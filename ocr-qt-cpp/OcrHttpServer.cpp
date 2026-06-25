// winsock2.h 必须在 windows.h 之前, 必须在最前面
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include "OcrHttpServer.h"
#include "OCRWrapper.h"
#include "OcrJson.h"
#include "PdfTextLayer.h"
#include "webui.h"
#include "ScreenshotDialog.h"
#include "TextClickEngine.h"
#include "TbpuLayout.h"
#include "PdfHelper.h"
#include "HtmlToXlsx.h"
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QFile>
#include <QEventLoop>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <iostream>
#include <cstring>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// ---- Winsock 初始化 (进程级, 只做一次) ----
static bool winsockInited = false;
static bool initWinsock() {
    if (winsockInited) return true;
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        std::cerr << "[http] WSAStartup failed: " << rc << std::endl;
        return false;
    }
    winsockInited = true;
    return true;
}

OcrHttpServer::OcrHttpServer(quint16 port, OCRWrapper* ocr, QObject* parent)
    : QObject(parent), m_listenSocket(INVALID_SOCKET), m_listenNotifier(nullptr),
      m_ocr(ocr), m_port(port)
{
    initWinsock();
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // 允许地址复用 (快速重启)
    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
}

OcrHttpServer::~OcrHttpServer() {
    // 关闭所有客户端
    for (auto it = m_bufs.begin(); it != m_bufs.end(); ++it) {
        closesocket(it.key());
    }
    m_bufs.clear();
    for (auto it = m_notifiers.begin(); it != m_notifiers.end(); ++it) {
        delete it.value();
    }
    m_notifiers.clear();
    // 关闭监听
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
}

bool OcrHttpServer::start() {
    if (m_listenSocket == INVALID_SOCKET) {
        qWarning() << "[http] invalid socket";
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        qWarning() << "[http] bind() failed";
        return false;
    }
    if (listen(m_listenSocket, 8) == SOCKET_ERROR) {
        qWarning() << "[http] listen() failed";
        return false;
    }

    // 用 QSocketNotifier 监听 socket 可读事件 (新连接到达)
    m_listenNotifier = new QSocketNotifier((int)m_listenSocket, QSocketNotifier::Read, this);
    connect(m_listenNotifier, &QSocketNotifier::activated, this, &OcrHttpServer::onListenReady);

    qDebug() << "[http] listening on port" << m_port;
    qDebug() << "[http] ready";
    return true;
}

quint16 OcrHttpServer::port() const {
    struct sockaddr_in addr;
    int len = sizeof(addr);
    if (getsockname(m_listenSocket, (struct sockaddr*)&addr, &len) == 0) {
        return ntohs(addr.sin_port);
    }
    return m_port;
}

void OcrHttpServer::setOcrActivityCallback(std::function<void()> cb) {
    m_onOcrActivity = std::move(cb);
}

void OcrHttpServer::setClipboardCallback(std::function<void(const QString&)> cb) {
    m_onClipboardCopy = std::move(cb);
}

void OcrHttpServer::onListenReady() {
    // 循环 accept 所有待处理连接
    while (true) {
        struct sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(m_listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break; // 没有更多连接了
            break;
        }

        // 设置非阻塞 (配合 QSocketNotifier 使用)
        u_long mode = 1;
        ioctlsocket(clientSock, FIONBIO, &mode);

        // 为客户端创建可读通知
        QSocketNotifier* notifier = new QSocketNotifier((int)clientSock, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this, clientSock]() {
            onClientReady((int)clientSock);
        });
        m_notifiers.insert(clientSock, notifier);
        ConnBuf cb;
        cb.clientIp = QByteArray(inet_ntoa(clientAddr.sin_addr));
        m_bufs.insert(clientSock, cb);
    }
}

void OcrHttpServer::closeClient(SOCKET s) {
    m_bufs.remove(s);
    auto it = m_notifiers.find(s);
    if (it != m_notifiers.end()) {
        delete it.value();
        m_notifiers.erase(it);
    }
    closesocket(s);
}

void OcrHttpServer::onClientReady(int socketFd) {
    SOCKET sock = (SOCKET)socketFd;
    if (m_bufs.find(sock) == m_bufs.end()) return; // 已关闭

    ConnBuf& buf = m_bufs[sock];

    // 读取所有可用数据
    char tmpBuf[65536];
    while (true) {
        int n = recv(sock, tmpBuf, sizeof(tmpBuf), 0);
        if (n > 0) {
            if (!buf.headerParsed) {
                buf.header += QByteArray(tmpBuf, n);
                // 只检查 \r\n\r\n 之前的头部大小 (不含 body)
                int headerEnd = buf.header.indexOf("\r\n\r\n");
                if (headerEnd < 0 && buf.header.size() > 65536) {
                    sendResponse(sock, 400, "text/plain; charset=utf-8", "header too large");
                    closeClient(sock);
                    return;
                }
            } else {
                buf.body += QByteArray(tmpBuf, n);
            }
        } else if (n == 0) {
            // 对端关闭连接
            closeClient(sock);
            return;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break; // 没有更多数据了
            closeClient(sock);
            return;
        }
    }

    // 还在解析请求头
    if (!buf.headerParsed) {
        int headerEnd = buf.header.indexOf("\r\n\r\n");
        if (headerEnd < 0) return; // 头还没读完

        // 头已完整
        QByteArray headerPart = buf.header.left(headerEnd);
        buf.body = buf.header.mid(headerEnd + 4);
        buf.header.clear();
        buf.headerParsed = true;

        // 解析请求行和头部
        QList<QByteArray> lines = headerPart.split('\n');
        for (int i = 0; i < lines.size(); i++) {
            QByteArray line = lines[i];
            if (!line.isEmpty() && line[line.size()-1] == '\r') line.chop(1);
            if (line.isEmpty()) continue;
            if (i == 0 || buf.method.isEmpty()) {
                QList<QByteArray> parts = line.split(' ');
                if (buf.method.isEmpty() && parts.size() >= 2) {
                    buf.method = parts[0];
                    buf.path = parts[1];
                    continue;
                }
            }
            int colon = line.indexOf(':');
            if (colon > 0) {
                QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
                QString val = QString::fromLatin1(line.mid(colon + 1)).trimmed();
                buf.headers[key] = val;
            }
        }
        buf.contentLength = buf.headers.value("content-length", "0").toInt();
    }

    // 检查 body 是否收完
    if (buf.headerParsed && buf.body.size() >= buf.contentLength) {
        handleRequest(sock, buf.method, buf.path, buf.body, buf.headers, buf.clientIp);
        // 请求处理完毕, 关闭连接 (Connection: close)
        closeClient(sock);
    }
}

void OcrHttpServer::handleRequest(SOCKET socket, const QByteArray& method,
                                   const QByteArray& path, const QByteArray& body,
                                   const QHash<QString, QString>& headers,
                                   const QByteArray& clientIp) {
    // 解析查询参数 (mode=coords, layout=1, layoutStrategy=multi_para, pdf=1 等)
    QHash<QString, QString> query = parseQuery(path);
    bool coords = (query.value("mode", "").toLower() == "coords");
    bool layout = (query.value("layout", "0").toLower() == "1" ||
                   query.value("layout", "0").toLower() == "true" ||
                   query.value("layout", "0").toLower() == "on");
    bool pdf = (query.value("pdf", "0").toLower() == "1" ||
                query.value("pdf", "0").toLower() == "true" ||
                query.value("pdf", "0").toLower() == "on");
    int tableMode = query.value("table", "0").toInt();  // 0=关, 1=SLANet, 2=img2table
    std::string layoutStrategy = query.value("layoutStrategy", "single_line").toStdString();
    bool doAngle = query.value("rotate", "1") != "0";

    // 去掉查询串得到纯路径
    QByteArray purePath = path;
    int q = purePath.indexOf('?');
    if (q >= 0) purePath = purePath.left(q);

    // GET /
    if (method == "GET" && (purePath == "/" )) {
        QByteArray html(WEBUI_HTML);
        sendResponse(socket, 200, "text/html; charset=utf-8", html);
        return;
    }

    // POST /ocr-raw  (body 直接是图片字节)
    if (method == "POST" && purePath == "/ocr-raw") {
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("OCR engine not ready").c_str());
            return;
        }
        QByteArray result = recognizeImage(body, coords, layout, layoutStrategy, pdf, tableMode, doAngle);

        sendResponse(socket, 200, "application/json; charset=utf-8", result);
        return;
    }

    // POST /ocr  (multipart/form-data, 支持多文件)
    if (method == "POST" && purePath == "/ocr") {
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("OCR engine not ready").c_str());
            return;
        }
        QString contentType = headers.value("content-type", "");
        QVector<QPair<QByteArray, QByteArray>> files = extractAllMultipartFiles(body, contentType.toLatin1());
        if (files.isEmpty()) {
            sendResponse(socket, 400, "application/json; charset=utf-8",
                         OcrJson::errorJson("no image in multipart data (field name must be 'file')").c_str());
            return;
        }
        if (files.size() == 1) {
            // 单文件: 使用 recognizeImageWithName 以支持 PDF
            QByteArray result = recognizeImageWithName(files[0].second, files[0].first, coords, layout, layoutStrategy, pdf, tableMode, doAngle);
            // 多页 PDF 会返回 NDJSON (多个 JSON 用 '\n' 拼接)。
            // 单个 JSON 里的换行已被 escapeJson 转义成两字符 "\n", 不会出现真实换行,
            // 因此只要含真实 '\n' 就一定是 NDJSON, 必须用 ndjson 的 Content-Type,
            // 否则网页端按单个 JSON 解析会报 "Unexpected non-whitespace character after JSON"。
            bool isNdjson = result.contains('\n');
            sendResponse(socket, 200,
                         isNdjson ? "application/x-ndjson; charset=utf-8"
                                  : "application/json; charset=utf-8",
                         result);
        } else {
            // 多文件: NDJSON, 每行含 file 字段
            QByteArray ndjson;
            for (const auto& f : files) {
                QByteArray result = recognizeImageWithName(f.second, f.first, coords, layout, layoutStrategy, pdf, tableMode, doAngle);
                ndjson += result + "\n";
            }
            sendResponse(socket, 200, "application/x-ndjson; charset=utf-8", ndjson);
        }
        return;
    }

    // POST /ocr-batch  (multipart 多文件批量, 始终返回 NDJSON)
    if (method == "POST" && purePath == "/ocr-batch") {
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("OCR engine not ready").c_str());
            return;
        }
        QString contentType = headers.value("content-type", "");
        QVector<QPair<QByteArray, QByteArray>> files = extractAllMultipartFiles(body, contentType.toLatin1());
        QByteArray ndjson;
        if (files.isEmpty()) {
            ndjson = OcrJson::errorJson("no image in multipart data").c_str();
            ndjson += "\n";
        } else {
            for (const auto& f : files) {
                QByteArray result = recognizeImageWithName(f.second, f.first, coords, layout, layoutStrategy, pdf, tableMode, doAngle);
                ndjson += result + "\n";
            }
        }
        sendResponse(socket, 200, "application/x-ndjson; charset=utf-8", ndjson);
        return;
    }

    // POST /ocr-pdf  (multipart, 直接返回可搜索 PDF 二进制)
    if (method == "POST" && purePath == "/ocr-pdf") {
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("OCR engine not ready").c_str());
            return;
        }
        if (m_onOcrActivity) m_onOcrActivity();
        QString contentType = headers.value("content-type", "");
        QVector<QPair<QByteArray, QByteArray>> files = extractAllMultipartFiles(body, contentType.toLatin1());
        if (files.isEmpty()) {
            sendResponse(socket, 400, "application/json; charset=utf-8",
                         OcrJson::errorJson("no image in multipart data").c_str());
            return;
        }
        // 收集所有文件 (含 PDF 多页展开)
        QVector<QPair<QByteArray, std::vector<OcrBlock>>> allPages;
        for (const auto& f : files) {
            // 用 recognizeImageWithName 的内部逻辑识别, 但这里需要收集原图+blocks
            // 复用: pdf=true 强制 coords=true, 内部 detectWithCoords
            QByteArray imgBytes = f.second;
            QString fname = QString::fromUtf8(f.first);
            if (fname.toLower().endsWith(".pdf")) {
                // PDF: 保存临时文件, 加载, 渲染每页, 收集
                QString tempPath = QDir::tempPath() + "/ocr_pdf_" +
                                  QString::number(QDateTime::currentMSecsSinceEpoch()) + ".pdf";
                QFile tempFile(tempPath);
                if (!tempFile.open(QIODevice::WriteOnly)) continue;
                tempFile.write(imgBytes);
                tempFile.close();
                if (!PdfHelper::load(tempPath)) { QFile::remove(tempPath); continue; }
                int pc = PdfHelper::pageCount();
                for (int i = 0; i < pc; i++) {
                    QPixmap pm = PdfHelper::renderPage(i);
                    if (pm.isNull()) continue;
                    QByteArray pageBmp;
                    QBuffer buf(&pageBmp);
                    buf.open(QIODevice::WriteOnly);
                    pm.save(&buf, "BMP");
                    buf.close();
                    std::vector<unsigned char> data(pageBmp.begin(), pageBmp.end());
                    std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
                    if (layout && !blocks.empty()) applyLayout(blocks, layoutStrategy);
                    // 保存原始 PNG 作为 PDF 覆盖图 (比 BMP 小很多)
                    QByteArray pagePng;
                    QBuffer pngBuf(&pagePng);
                    pngBuf.open(QIODevice::WriteOnly);
                    pm.save(&pngBuf, "PNG");
                    pngBuf.close();
                    allPages.append(qMakePair(pagePng, blocks));
                }
                PdfHelper::close();
                QFile::remove(tempPath);
            } else {
                // 普通图片: 先解码获取原始尺寸, 转 BMP 给 OCR, 保留原图字节
                QImage img;
                if (!img.loadFromData(imgBytes)) continue;
                if (img.hasAlphaChannel()) {
                    QImage filled(img.size(), QImage::Format_RGB32);
                    filled.fill(Qt::white);
                    QPainter p(&filled);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = filled;
                }
                QByteArray bmpBytes;
                QBuffer buf(&bmpBytes);
                buf.open(QIODevice::WriteOnly);
                img.save(&buf, "BMP");
                buf.close();
        std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());

        std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
                if (layout && !blocks.empty()) applyLayout(blocks, layoutStrategy);
                // 保存 PNG 作为 PDF 覆盖图
                QByteArray pngBytes;
                QBuffer pngBuf(&pngBytes);
                pngBuf.open(QIODevice::WriteOnly);
                img.save(&pngBuf, "PNG");
                pngBuf.close();
                allPages.append(qMakePair(pngBytes, blocks));
            }
        }
        if (allPages.isEmpty()) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("failed to recognize any image").c_str());
            return;
        }
        QByteArray pdfBytes;
        PdfTextLayer::buildSearchablePdf(pdfBytes, allPages);
        sendResponse(socket, 200, "application/pdf", pdfBytes);
        return;
    }

    // POST /screenshot  (触发服务器端截图框选, 识别后返回 JSON)
    // 安全限制: 仅允许本机访问 (截图涉及屏幕内容)
    if (method == "POST" && purePath == "/screenshot") {
        if (clientIp != "127.0.0.1" && clientIp != "::1") {
            sendResponse(socket, 403, "application/json; charset=utf-8",
                         OcrJson::errorJson("screenshot is only available from localhost").c_str());
            return;
        }
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         OcrJson::errorJson("OCR engine not ready").c_str());
            return;
        }
        if (m_onOcrActivity) m_onOcrActivity();
        // 在主线程执行截图 (HTTP 在 socket 线程, 必须切到主线程弹 GUI)
        QPixmap cropped = doScreenshotOnMainThread();
        if (cropped.isNull()) {
            sendResponse(socket, 200, "application/json; charset=utf-8",
                         OcrJson::errorJson("screenshot cancelled").c_str());
            return;
        }
        // QPixmap -> BMP -> 识别
        QByteArray bmpBytes;
        {
            QBuffer buf(&bmpBytes);
            buf.open(QIODevice::WriteOnly);
            QPixmap p = cropped;
            if (p.hasAlpha()) {
                QPixmap filled(p.size());
                filled.fill(Qt::white);
                QPainter pt(&filled);
                pt.drawPixmap(0, 0, p);
                pt.end();
                p = filled;
            }
            p.save(&buf, "BMP");
        }
        std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());
        std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
        // 排版模式: 对 blocks 原地排序 + 写 end 分隔符
        if (layout && !blocks.empty()) {
            applyLayout(blocks, layoutStrategy);
        }
        QByteArray result = (coords || layout) ? OcrJson::blocksToJson(blocks).c_str()
                                               : OcrJson::textOnlyToJson(blocks).c_str();
        sendResponse(socket, 200, "application/json; charset=utf-8", result);
        return;
    }

    // POST /textclick  (TextClick 风格 JSON API, 支持截屏/图片 OCR + 鼠标点击/移动 + 坐标查询)
    // 安全限制: 仅允许本机访问 (涉及屏幕内容+鼠标操控)
    if (method == "POST" && purePath == "/textclick") {
        if (clientIp != "127.0.0.1" && clientIp != "::1") {
            sendResponse(socket, 403, "application/json; charset=utf-8",
                         "{\"success\":false,\"data\":\"\",\"message\":\"textclick is only available from localhost\"}");
            return;
        }
        if (!m_ocr) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         "{\"success\":false,\"data\":\"\",\"message\":\"OCR engine not ready\"}");
            return;
        }
        QByteArray result = handleTextClick(body);
        sendResponse(socket, 200, "application/json; charset=utf-8", result);
        return;
    }

    // POST /copy-clipboard  接收 HTML 并写入系统剪贴板 (与 GUI 自动复制一致)
    if (method == "POST" && purePath == "/copy-clipboard") {
        if (clientIp != "127.0.0.1" && clientIp != "::1") {
            sendResponse(socket, 403, "application/json; charset=utf-8",
                         "{\"success\":false,\"message\":\"only available from localhost\"}");
            return;
        }
        QString html = QString::fromUtf8(body);
        if (m_onClipboardCopy) {
            m_onClipboardCopy(html);
            sendResponse(socket, 200, "application/json; charset=utf-8",
                         "{\"success\":true,\"message\":\"copied\"}");
        } else {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         "{\"success\":false,\"message\":\"clipboard callback not set\"}");
        }
        return;
    }

    // GET /api/config  获取当前 OCR 参数配置
    if (method == "GET" && purePath == "/api/config") {
        QJsonObject o;
        o["doAngle"]        = m_ocr->doAngle() != 0;
        o["padding"]        = m_ocr->padding();
        o["maxSideLen"]     = m_ocr->maxSideLen();
        o["boxScoreThresh"] = (double)m_ocr->boxScoreThresh();
        o["boxThresh"]      = (double)m_ocr->boxThresh();
        o["unClipRatio"]    = (double)m_ocr->unClipRatio();
        o["tableBoxScoreThresh"] = (double)m_ocr->tableBoxScoreThresh();
        o["tableUnClipRatio"]    = (double)m_ocr->tableUnClipRatio();
        QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
        sendResponse(socket, 200, "application/json; charset=utf-8", json);
        return;
    }

    // POST /api/config  修改 OCR 参数配置 (全局生效, 写入 ocr_config.json)
    if (method == "POST" && purePath == "/api/config") {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            sendResponse(socket, 400, "application/json; charset=utf-8",
                         "{\"error\":\"invalid json\"}");
            return;
        }
        QJsonObject o = doc.object();
        if (o.contains("doAngle"))        m_ocr->setDoAngle(o["doAngle"].toBool() ? 1 : 0);
        if (o.contains("padding"))        m_ocr->setPadding(o["padding"].toInt());
        if (o.contains("maxSideLen"))     m_ocr->setMaxSideLen(o["maxSideLen"].toInt());
        if (o.contains("boxScoreThresh")) m_ocr->setBoxScoreThresh((float)o["boxScoreThresh"].toDouble());
        if (o.contains("boxThresh"))      m_ocr->setBoxThresh((float)o["boxThresh"].toDouble());
        if (o.contains("unClipRatio"))    m_ocr->setUnClipRatio((float)o["unClipRatio"].toDouble());
        if (o.contains("tableBoxScoreThresh")) m_ocr->setTableBoxScoreThresh((float)o["tableBoxScoreThresh"].toDouble());
        if (o.contains("tableUnClipRatio"))    m_ocr->setTableUnClipRatio((float)o["tableUnClipRatio"].toDouble());
        // 写入全局配置文件
        m_ocr->saveConfig();
        sendResponse(socket, 200, "application/json; charset=utf-8",
                     "{\"ok\":true}");
        return;
    }

    // 未知路由
    sendResponse(socket, 404, "application/json; charset=utf-8",
                 OcrJson::errorJson("not found, use GET / or POST /ocr, /ocr-raw, /ocr-pdf, /screenshot, /textclick, /copy-clipboard, /api/config").c_str());
}

// 构建表格模式 JSON (含 xlsx base64)
static std::string buildTableJson(const QByteArray& filename, const std::string& html,
                                   const std::string& text, float score) {
    static const char tb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string xlsxB64;
    QString tmpXlsx = QDir::tempPath() + "/ocr_table_" +
                      QString::number(QDateTime::currentMSecsSinceEpoch()) + ".xlsx";
    std::string tmpXlsxStr = tmpXlsx.toStdString();
    if (htmlToXlsx(html, tmpXlsxStr)) {
        QFile tmpFile(tmpXlsx);
        if (tmpFile.open(QIODevice::ReadOnly)) {
            QByteArray xlsxData = tmpFile.readAll();
            tmpFile.close();
            size_t len = xlsxData.size();
            xlsxB64.reserve(((len + 2) / 3) * 4);
            const char* raw = xlsxData.constData();
            size_t i = 0;
            while (i + 2 < len) {
                unsigned int n = (unsigned char)raw[i]<<16 | (unsigned char)raw[i+1]<<8 | (unsigned char)raw[i+2];
                xlsxB64 += tb64[(n>>18)&0x3F]; xlsxB64 += tb64[(n>>12)&0x3F];
                xlsxB64 += tb64[(n>>6)&0x3F];  xlsxB64 += tb64[n&0x3F];
                i += 3;
            }
            size_t rem = len - i;
            if (rem == 1) { unsigned int n=(unsigned char)raw[i]<<16; xlsxB64+=tb64[(n>>18)&0x3F]; xlsxB64+=tb64[(n>>12)&0x3F]; xlsxB64+="=="; }
            else if (rem == 2) { unsigned int n=(unsigned char)raw[i]<<16|(unsigned char)raw[i+1]<<8; xlsxB64+=tb64[(n>>18)&0x3F]; xlsxB64+=tb64[(n>>12)&0x3F]; xlsxB64+=tb64[(n>>6)&0x3F]; xlsxB64+="="; }
        }
        QFile::remove(tmpXlsx);
    }

    auto escapeStr = [](const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    };

    std::string json = "{\"code\":0,\"type\":\"table\"";
    if (!filename.isEmpty()) {
        json += ",\"file\":\"" + escapeStr(filename.constData()) + "\"";
    }
    json += ",\"text\":\"" + escapeStr(text) + "\"";
    json += ",\"html\":\"" + escapeStr(html) + "\"";
    json += ",\"structureScore\":" + std::to_string(score);
    if (!xlsxB64.empty()) {
        json += ",\"xlsx\":\"" + xlsxB64 + "\"";
    }
    json += "}";
    return json;
}

// 识别图片字节: 先转 BMP (绕过 libjpeg 冲突), 再调 OCR
// coords=true 返回带坐标的 blocks, false 仅返回 text
// layout=true 启用排版模式; layoutStrategy 排版策略 key
// pdf=true 时在 JSON 里附加 "pdf" 字段 (base64 可搜索 PDF)
QByteArray OcrHttpServer::recognizeImage(const QByteArray& imageBytes,
                                           bool coords, bool layout,
                                           const std::string& layoutStrategy,
                                           bool pdf, int tableMode, bool doAngle) {
    if (m_onOcrActivity) m_onOcrActivity();
    try {
        if (m_ocr) {
            m_ocr->setDoAngle(doAngle ? 1 : 0);
        }
        QImage img;
        if (!img.loadFromData(imageBytes)) {
            return OcrJson::errorJson("cannot decode image").c_str();
        }
        if (img.hasAlphaChannel()) {
            QImage filled(img.size(), QImage::Format_RGB32);
            filled.fill(Qt::white);
            QPainter p(&filled);
            p.drawImage(0, 0, img);
            p.end();
            img = filled;
        }
        QByteArray bmpBytes;
        QBuffer buf(&bmpBytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "BMP");
        buf.close();
        std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());

        if (tableMode != 0) {
            m_ocr->setTableMode(tableMode);
            auto tableResult = m_ocr->detectTable(data);
            return QByteArray::fromStdString(
                buildTableJson(QByteArray(), tableResult.htmlStructure,
                               tableResult.ocrText, tableResult.structureScore));
        }

        std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
        if (layout && !blocks.empty()) {
            try { applyLayout(blocks, layoutStrategy); } catch (...) {}
        }

        std::string pdfB64;
        if (pdf) {
            try {
                QByteArray pngBytes;
                QBuffer pngBuf(&pngBytes);
                pngBuf.open(QIODevice::WriteOnly);
                img.save(&pngBuf, "PNG");
                pngBuf.close();
                QByteArray pdfBytes;
                QVector<QPair<QByteArray, std::vector<OcrBlock>>> pages;
                pages.append(qMakePair(pngBytes, blocks));
                PdfTextLayer::buildSearchablePdf(pdfBytes, pages);
                static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::string raw(pdfBytes.data(), pdfBytes.size());
                size_t len = raw.size();
                pdfB64.reserve(((len + 2) / 3) * 4);
                size_t i = 0;
                while (i + 2 < len) {
                    unsigned int n = (unsigned char)raw[i]<<16 | (unsigned char)raw[i+1]<<8 | (unsigned char)raw[i+2];
                    pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F];
                    pdfB64 += tb[(n>>6)&0x3F];  pdfB64 += tb[n&0x3F];
                    i += 3;
                }
                size_t rem = len - i;
                if (rem == 1) {
                    unsigned int n = (unsigned char)raw[i]<<16;
                    pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F]; pdfB64 += "==";
                } else if (rem == 2) {
                    unsigned int n = (unsigned char)raw[i]<<16 | (unsigned char)raw[i+1]<<8;
                    pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F];
                    pdfB64 += tb[(n>>6)&0x3F];  pdfB64 += "=";
                }
            } catch (...) {}
        }

        return (coords || layout) ? OcrJson::blocksToJson(blocks, "", pdfB64).c_str()
                                  : OcrJson::textOnlyToJson(blocks, "", pdfB64).c_str();
    } catch (const std::exception& e) {
        return OcrJson::errorJson(e.what()).c_str();
    } catch (...) {
        return OcrJson::errorJson("unknown error").c_str();
    }
}

// 识别图片字节并附带文件名 (多文件模式)
QByteArray OcrHttpServer::recognizeImageWithName(const QByteArray& imageBytes,
                                                   const QByteArray& filename,
                                                   bool coords, bool layout,
                                                   const std::string& layoutStrategy,
                                                   bool pdf, int tableMode, bool doAngle) {
    if (m_onOcrActivity) m_onOcrActivity();
    try {
        if (m_ocr) {
            m_ocr->setDoAngle(doAngle ? 1 : 0);
        }
        QVector<QPair<QByteArray, std::vector<OcrBlock>>> pdfPages;

        // 检查是否是 PDF 文件 (根据文件名)
        QString fname = QString::fromUtf8(filename);
        if (fname.toLower().endsWith(".pdf")) {
            // PDF 文件: 需要将字节保存到临时文件, 然后加载
            QString tempPath = QDir::tempPath() + "/ocr_pdf_" + 
                              QString::number(QDateTime::currentMSecsSinceEpoch()) + ".pdf";
            QFile tempFile(tempPath);
            if (!tempFile.open(QIODevice::WriteOnly)) {
                return OcrJson::fileErrorJson(filename.constData(), "cannot create temp file for PDF").c_str();
            }
            tempFile.write(imageBytes);
            tempFile.close();

            // 加载 PDF
            if (!PdfHelper::load(tempPath)) {
                QFile::remove(tempPath);
                return OcrJson::fileErrorJson(filename.constData(), "cannot load PDF").c_str();
            }

            int pageCount = PdfHelper::pageCount();
            if (pageCount <= 0) {
                PdfHelper::close();
                QFile::remove(tempPath);
                return OcrJson::fileErrorJson(filename.constData(), "PDF has no pages").c_str();
            }

            // 单页 PDF: 直接识别
            if (pageCount == 1) {
                try {
                    QPixmap pixmap = PdfHelper::renderPage(0);
                    PdfHelper::close();
                    QFile::remove(tempPath);
                    if (pixmap.isNull()) {
                        return OcrJson::fileErrorJson(filename.constData(), "cannot render PDF page").c_str();
                    }
                    // QPixmap -> BMP
                    QByteArray bmpBytes;
                    QBuffer buf(&bmpBytes);
                    buf.open(QIODevice::WriteOnly);
                    pixmap.save(&buf, "BMP");
                    buf.close();
                    std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());

        if (tableMode != 0) {
            m_ocr->setTableMode(tableMode);
            auto tableResult = m_ocr->detectTable(data);
            return QByteArray::fromStdString(
                buildTableJson(QByteArray(), tableResult.htmlStructure,
                               tableResult.ocrText, tableResult.structureScore));
        }

                    std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
                    if (layout && !blocks.empty()) {
                        try {
                            applyLayout(blocks, layoutStrategy);
                        } catch (const std::exception& e) {
                            qDebug() << "[http] layout failed on PDF page:" << e.what();
                        } catch (...) {
                            qDebug() << "[http] layout failed on PDF page: unknown error";
                        }
                    }
                    // pdf: 收集页数据并生成 base64
                    if (pdf) {
                        QByteArray pngBytes;
                        QBuffer pngBuf(&pngBytes);
                        pngBuf.open(QIODevice::WriteOnly);
                        pixmap.save(&pngBuf, "PNG");
                        pngBuf.close();
                        pdfPages.append(qMakePair(pngBytes, blocks));
                    }
                    std::string pdfB64;
                    if (pdf && !pdfPages.isEmpty()) {
                        try {
                            QByteArray pdfBytes;
                            PdfTextLayer::buildSearchablePdf(pdfBytes, pdfPages);
                            // base64 编码
                            static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                            std::string raw(pdfBytes.data(), pdfBytes.size());
                            size_t len = raw.size();
                            pdfB64.reserve(((len+2)/3)*4);
                            size_t j = 0;
                            while (j + 2 < len) {
                                unsigned int n = (unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8|(unsigned char)raw[j+2];
                                pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F];
                                pdfB64 += tb[(n>>6)&0x3F];  pdfB64 += tb[n&0x3F]; j += 3;
                            }
                            size_t rem = len - j;
                            if (rem == 1) { unsigned int n=(unsigned char)raw[j]<<16; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+="=="; }
                            else if (rem == 2) { unsigned int n=(unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+=tb[(n>>6)&0x3F]; pdfB64+="="; }
                        } catch (const std::exception& e) {
                            qDebug() << "[http] PDF generation failed:" << e.what();
                        } catch (...) {
                            qDebug() << "[http] PDF generation failed: unknown error";
                        }
                    }
                    return (coords || layout) ? OcrJson::blocksToJson(blocks, filename.constData(), pdfB64).c_str()
                                              : OcrJson::textOnlyToJson(blocks, filename.constData(), pdfB64).c_str();
                } catch (const std::exception& e) {
                    PdfHelper::close();
                    QFile::remove(tempPath);
                    return OcrJson::fileErrorJson(filename.constData(), e.what()).c_str();
                } catch (...) {
                    PdfHelper::close();
                    QFile::remove(tempPath);
                    return OcrJson::fileErrorJson(filename.constData(), "unknown error").c_str();
                }
            }

            // 多页 PDF: 返回所有页面的结果 (NDJSON 格式)
            QByteArray ndjson;
            for (int i = 0; i < pageCount; i++) {
                try {
                    QPixmap pixmap = PdfHelper::renderPage(i);
                    if (pixmap.isNull()) {
                        ndjson += OcrJson::fileErrorJson(filename.constData(), 
                                 QString("cannot render PDF page %1").arg(i+1).toUtf8().constData()).c_str();
                        ndjson += "\n";
                        continue;
                    }
                    QByteArray bmpBytes;
                    QBuffer buf(&bmpBytes);
                    buf.open(QIODevice::WriteOnly);
                    pixmap.save(&buf, "BMP");
                    buf.close();
                    std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());

                    if (tableMode != 0) {
                        // 表格模式：调用 detectTable
                        m_ocr->setTableMode(tableMode);
                        auto tableResult = m_ocr->detectTable(data);
                        QByteArray pageFilename = filename + "_page" + QByteArray::number(i + 1);
                        ndjson += QByteArray::fromStdString(
                            buildTableJson(pageFilename, tableResult.htmlStructure,
                                           tableResult.ocrText, tableResult.structureScore));
                        ndjson += "\n";
                    } else {
                        // 普通模式
                        std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
                        if (layout && !blocks.empty()) {
                            try {
                                applyLayout(blocks, layoutStrategy);
                            } catch (const std::exception& e) {
                                qDebug() << "[http] layout failed on PDF page" << (i+1) << ":" << e.what();
                            } catch (...) {
                                qDebug() << "[http] layout failed on PDF page" << (i+1) << ": unknown error";
                            }
                        }
                        // pdf: 收集每页 PNG+blocks
                        if (pdf) {
                            QByteArray pngBytes;
                            QBuffer pngBuf(&pngBytes);
                            pngBuf.open(QIODevice::WriteOnly);
                            pixmap.save(&pngBuf, "PNG");
                            pngBuf.close();
                            pdfPages.append(qMakePair(pngBytes, blocks));
                        }
                        // 每页添加页码标识
                        QByteArray pageFilename = filename + "_page" + QByteArray::number(i + 1);
                        ndjson += (coords || layout) ? OcrJson::blocksToJson(blocks, pageFilename.constData()).c_str()
                                                     : OcrJson::textOnlyToJson(blocks, pageFilename.constData()).c_str();
                        ndjson += "\n";
                    }
                } catch (const std::exception& e) {
                    ndjson += OcrJson::fileErrorJson(filename.constData(), 
                             QString("page %1 failed: %2").arg(i+1).arg(e.what()).toUtf8().constData()).c_str();
                    ndjson += "\n";
                } catch (...) {
                    ndjson += OcrJson::fileErrorJson(filename.constData(), 
                             QString("page %1 failed: unknown error").arg(i+1).toUtf8().constData()).c_str();
                    ndjson += "\n";
                }
            }
            PdfHelper::close();
            QFile::remove(tempPath);

            // pdf 模式: 多页 PDF 合并成一个可搜索 PDF, base64 附加到最后一行 JSON
            if (pdf && !pdfPages.isEmpty()) {
                try {
                    QByteArray pdfBytes;
                    PdfTextLayer::buildSearchablePdf(pdfBytes, pdfPages);
                    // base64
                    static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    std::string raw(pdfBytes.data(), pdfBytes.size());
                    size_t len = raw.size();
                    std::string pdfB64;
                    pdfB64.reserve(((len+2)/3)*4);
                    size_t j = 0;
                    while (j + 2 < len) {
                        unsigned int n = (unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8|(unsigned char)raw[j+2];
                        pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F];
                        pdfB64 += tb[(n>>6)&0x3F];  pdfB64 += tb[n&0x3F]; j += 3;
                    }
                    size_t rem = len - j;
                    if (rem == 1) { unsigned int n=(unsigned char)raw[j]<<16; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+="=="; }
                    else if (rem == 2) { unsigned int n=(unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+=tb[(n>>6)&0x3F]; pdfB64+="="; }
                    // 在 NDJSON 最后加一个汇总行, 含 pdf 字段 (前端优先读此行的 pdf)
                    QByteArray summaryLine = "{\"code\":0,\"pdfPages\":" + QByteArray::number(pdfPages.size()) + ",\"pdf\":\"" + QByteArray(pdfB64.c_str(), pdfB64.size()) + "\"}";
                    ndjson += summaryLine + "\n";
                } catch (const std::exception& e) {
                    qDebug() << "[http] PDF generation failed:" << e.what();
                } catch (...) {
                    qDebug() << "[http] PDF generation failed: unknown error";
                }
            }
            return ndjson;
        }

        // 普通图片文件
        QImage img;
        if (!img.loadFromData(imageBytes)) {
            return OcrJson::fileErrorJson(filename.constData(), "cannot decode image").c_str();
        }
        if (img.hasAlphaChannel()) {
            QImage filled(img.size(), QImage::Format_RGB32);
            filled.fill(Qt::white);
            QPainter p(&filled);
            p.drawImage(0, 0, img);
            p.end();
            img = filled;
        }
        QByteArray bmpBytes;
        QBuffer buf(&bmpBytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "BMP");
        buf.close();

        std::vector<unsigned char> data(bmpBytes.begin(), bmpBytes.end());

        if (tableMode != 0) {
            m_ocr->setTableMode(tableMode);
            auto tableResult = m_ocr->detectTable(data);
            return QByteArray::fromStdString(
                buildTableJson(filename, tableResult.htmlStructure,
                               tableResult.ocrText, tableResult.structureScore));
        }

        std::vector<OcrBlock> blocks = m_ocr->detectWithCoords(data);
        if (layout && !blocks.empty()) {
            try { applyLayout(blocks, layoutStrategy); } catch (...) {}
        }

        std::string pdfB64;
        if (pdf) {
            try {
                QByteArray pngBytes;
                QBuffer pngBuf(&pngBytes);
                pngBuf.open(QIODevice::WriteOnly);
                img.save(&pngBuf, "PNG");
                pngBuf.close();
                QByteArray pdfBytes;
                QVector<QPair<QByteArray, std::vector<OcrBlock>>> pages;
                pages.append(qMakePair(pngBytes, blocks));
                PdfTextLayer::buildSearchablePdf(pdfBytes, pages);
                static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::string raw(pdfBytes.data(), pdfBytes.size());
                size_t len = raw.size();
                pdfB64.reserve(((len+2)/3)*4);
                size_t j = 0;
                while (j + 2 < len) {
                    unsigned int n = (unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8|(unsigned char)raw[j+2];
                    pdfB64 += tb[(n>>18)&0x3F]; pdfB64 += tb[(n>>12)&0x3F];
                    pdfB64 += tb[(n>>6)&0x3F];  pdfB64 += tb[n&0x3F]; j += 3;
                }
                size_t rem = len - j;
                if (rem == 1) { unsigned int n=(unsigned char)raw[j]<<16; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+="=="; }
                else if (rem == 2) { unsigned int n=(unsigned char)raw[j]<<16|(unsigned char)raw[j+1]<<8; pdfB64+=tb[(n>>18)&0x3F]; pdfB64+=tb[(n>>12)&0x3F]; pdfB64+=tb[(n>>6)&0x3F]; pdfB64+="="; }
            } catch (...) {}
        }

        return (coords || layout) ? OcrJson::blocksToJson(blocks, filename.constData(), pdfB64).c_str()
                                  : OcrJson::textOnlyToJson(blocks, filename.constData(), pdfB64).c_str();
    } catch (const std::exception& e) {
        return OcrJson::fileErrorJson(filename.constData(), e.what()).c_str();
    } catch (...) {
        return OcrJson::fileErrorJson(filename.constData(), "unknown error").c_str();
    }
}

// 从 path (含查询串) 解析查询参数
QHash<QString, QString> OcrHttpServer::parseQuery(const QByteArray& pathWithQuery) {
    QHash<QString, QString> result;
    int q = pathWithQuery.indexOf('?');
    if (q < 0) return result;
    QByteArray queryStr = pathWithQuery.mid(q + 1);
    QList<QByteArray> pairs = queryStr.split('&');
    for (const QByteArray& pair : pairs) {
        int eq = pair.indexOf('=');
        if (eq > 0) {
            QString key = QString::fromLatin1(pair.left(eq));
            QString val = QString::fromLatin1(pair.mid(eq + 1));
            result[key] = val;
        } else if (!pair.isEmpty()) {
            result[QString::fromLatin1(pair)] = "";
        }
    }
    return result;
}

// 在主线程执行截图框选 (HTTP 在 socket 线程, 弹 GUI 必须切到主线程)
QPixmap OcrHttpServer::doScreenshotOnMainThread() {
    QPixmap result;
    QMetaObject::invokeMethod(this, [this, &result]() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QPixmap fullScreen = screen->grabWindow(0);
        if (fullScreen.isNull()) return;
        ScreenshotDialog dlg(fullScreen, false);
        QEventLoop loop;
        connect(&dlg, &ScreenshotDialog::closed, [&](const QPixmap& pix) {
            result = pix;
            loop.quit();
        });
        connect(&dlg, &ScreenshotDialog::destroyed, [&]() { loop.quit(); });
        dlg.showFullScreen();
        loop.exec();
    }, Qt::BlockingQueuedConnection);
    return result;
}

// 解析 multipart/form-data, 提取第一个文件部分的内容
QByteArray OcrHttpServer::extractMultipartFile(const QByteArray& body, const QByteArray& contentType) {
    auto files = extractAllMultipartFiles(body, contentType);
    return files.isEmpty() ? QByteArray() : files[0].second;
}

// 解析 multipart/form-data, 提取所有文件部分
// 返回: QVector<QPair<filename, data>>
QVector<QPair<QByteArray, QByteArray>> OcrHttpServer::extractAllMultipartFiles(
        const QByteArray& body, const QByteArray& contentType) {
    QVector<QPair<QByteArray, QByteArray>> result;
    int bpos = contentType.indexOf("boundary=");
    if (bpos < 0) return result;
    QByteArray boundary = contentType.mid(bpos + 9).trimmed();
    if (boundary.startsWith('"') && boundary.endsWith('"')) {
        boundary = boundary.mid(1, boundary.size() - 2);
    }
    QByteArray firstDelim = "--" + boundary;
    QByteArray nextDelim = "\r\n--" + boundary;

    int pos = 0;
    while (true) {
        int start = body.indexOf(firstDelim, pos);
        if (start < 0) break;

        // 跳到 header 区域结束 (\r\n\r\n)
        int headerEnd = body.indexOf("\r\n\r\n", start);
        if (headerEnd < 0) break;
        int dataStart = headerEnd + 4;

        // 找下一个 boundary
        int end = body.indexOf(nextDelim, dataStart);
        if (end < 0) break;

        QByteArray partData = body.mid(dataStart, end - dataStart);

        // 从 Content-Disposition 解析 filename
        QByteArray headerPart = body.mid(start, headerEnd - start);
        QByteArray filename;
        int fnPos = headerPart.indexOf("filename=");
        if (fnPos >= 0) {
            int fnStart = fnPos + 9;
            if (fnStart < headerPart.size() && headerPart[fnStart] == '"') {
                fnStart++;
                int fnEnd = headerPart.indexOf('"', fnStart);
                if (fnEnd >= 0) {
                    filename = headerPart.mid(fnStart, fnEnd - fnStart);
                }
            }
        }
        if (filename.isEmpty()) {
            filename = "file" + QByteArray::number(result.size() + 1);
        }

        result.append(qMakePair(filename, partData));

        pos = start + firstDelim.size();
    }

    return result;
}

void OcrHttpServer::sendResponse(SOCKET socket, int code, const QByteArray& contentType,
                                  const QByteArray& body) {
    QByteArray reason;
    switch (code) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 404: reason = "Not Found"; break;
        case 500: reason = "Internal Server Error"; break;
        default:  reason = "OK"; break;
    }
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;

    // 分块发送 (大图片可能超过单次 send 缓冲区)
    const char* ptr = resp.constData();
    int remaining = resp.size();
    while (remaining > 0) {
        int n = send(socket, ptr, remaining, 0);
        if (n <= 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                Sleep(1);
                continue;
            }
            break;
        }
        ptr += n;
        remaining -= n;
    }
}

// ============================================================
// TextClick 风格 JSON API 处理
// ============================================================

// 解码 JSON Unicode 转义 (\uXXXX -> UTF-8)
static QByteArray decodeJsonUnicode(const QByteArray& s) {
    QByteArray out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 'u' && i + 5 < s.size()) {
                // \uXXXX
                QByteArray hex = s.mid(i+2, 4);
                int codepoint = 0;
                for (int j = 0; j < 4; j++) {
                    char c = hex[j];
                    int digit = 0;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    codepoint = (codepoint << 4) | digit;
                }
                // 转为 UTF-8
                if (codepoint < 0x80) {
                    out += (char)codepoint;
                } else if (codepoint < 0x800) {
                    out += (char)(0xC0 | (codepoint >> 6));
                    out += (char)(0x80 | (codepoint & 0x3F));
                } else {
                    out += (char)(0xE0 | (codepoint >> 12));
                    out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    out += (char)(0x80 | (codepoint & 0x3F));
                }
                i += 5;
            } else if (s[i+1] == '\\') { out += '\\'; i++; }
            else if (s[i+1] == '"') { out += '"'; i++; }
            else if (s[i+1] == 'n') { out += '\n'; i++; }
            else if (s[i+1] == 'r') { out += '\r'; i++; }
            else if (s[i+1] == 't') { out += '\t'; i++; }
            else { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

// 手写 JSON 解析 (不引入第三方库)
static QString jsonGetString(const QByteArray& json, const QString& key, const QString& def = QString()) {
    // 查找 "key": "value" 或 "key": value
    QByteArray keyBytes = ("\"" + key + "\"").toLatin1();
    int pos = json.indexOf(keyBytes);
    if (pos < 0) return def;
    pos += keyBytes.length();
    // 过空白和冒号
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.length()) return def;
    // 字符串值
    if (json[pos] == '"') {
        pos++;
        int end = json.indexOf('"', pos);
        if (end < 0) return def;
        QByteArray raw = json.mid(pos, end - pos);
        QByteArray decoded = decodeJsonUnicode(raw);
        return QString::fromUtf8(decoded);
    }
    // 非字符串值 (数字/bool/null), 直接返回默认
    return def;
}

static int jsonGetInt(const QByteArray& json, const QString& key, int def = 0) {
    QByteArray keyBytes = ("\"" + key + "\"").toLatin1();
    int pos = json.indexOf(keyBytes);
    if (pos < 0) return def;
    pos += keyBytes.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.length()) return def;
    // 数字值
    int start = pos;
    bool negative = false;
    if (json[pos] == '-') { negative = true; pos++; }
    while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9') pos++;
    if (pos > start) {
        QByteArray numStr = json.mid(start, pos - start);
        return numStr.toInt();
    }
    return def;
}

// 解析 region: "x,y,w,h"
static bool parseRegionStr(const QString& str, int& x, int& y, int& w, int& h) {
    QStringList parts = str.split(',');
    if (parts.size() != 4) return false;
    x = parts[0].toInt();
    y = parts[1].toInt();
    w = parts[2].toInt();
    h = parts[3].toInt();
    return true;
}

// JSON 字符串转义
static QByteArray jsonEscape(const QByteArray& s) {
    QByteArray out;
    out.reserve(s.length() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// 输出 TextClick 风格结果 JSON
static QByteArray textclickResult(bool success, const QByteArray& data, const QByteArray& message) {
    return "{\"success\":" + QByteArray(success ? "true" : "false")
           + ",\"data\":\"" + jsonEscape(data) + "\""
           + ",\"message\":\"" + jsonEscape(message) + "\"}";
}

// list action: data 是 JSON 数组, 不套引号
static QByteArray textclickListResult(TextClickEngine& eng) {
    QByteArray arr = "[";
    int n = eng.blockCount();
    for (int i = 0; i < n; i++) {
        if (i > 0) arr += ",";
        int box[8];
        eng.getBlockBox(i, box);
        arr += "{\"index\":" + QByteArray::number(i)
               + ",\"text\":\"" + jsonEscape(eng.getBlockText(i).c_str()) + "\""
               + ",\"box\":[" + QByteArray::number(box[0]) + "," + QByteArray::number(box[1])
               + "," + QByteArray::number(box[2]) + "," + QByteArray::number(box[3])
               + "," + QByteArray::number(box[4]) + "," + QByteArray::number(box[5])
               + "," + QByteArray::number(box[6]) + "," + QByteArray::number(box[7]) + "]"
               + ",\"score\":" + QByteArray::number(eng.getBlockScore(i), 'f', 2) + "}";
    }
    arr += "]";
    return "{\"success\":true,\"data\":" + arr + ",\"message\":\"识别完成\"}";
}

QByteArray OcrHttpServer::handleTextClick(const QByteArray& jsonBody) {
    // 解析 JSON 参数
    QString action = jsonGetString(jsonBody, "action");
    QString text   = jsonGetString(jsonBody, "text");
    int occurrence = jsonGetInt(jsonBody, "occurrence", 1);
    QString location = jsonGetString(jsonBody, "location", "center");
    QString regionStr = jsonGetString(jsonBody, "region");
    QString imagePath = jsonGetString(jsonBody, "image");

    if (action.isEmpty()) {
        return textclickResult(false, "", "缺少 action 参数");
    }

    // 创建引擎
    TextClickEngine eng(m_ocr);
    TextClickEngine::Location loc = TextClickEngine::parseLocation(location.toStdString());

    // 截屏 或 识别图片
    int blockCount = 0;
    if (!imagePath.isEmpty()) {
        blockCount = eng.detectImage(imagePath.toStdString());
        if (blockCount < 0) {
            return textclickResult(false, "", "图片识别失败");
        }
    } else {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (!regionStr.isEmpty()) {
            parseRegionStr(regionStr, rx, ry, rw, rh);
        }
        // 程序化截图 (非交互, 不弹框)
        blockCount = eng.detectScreen(rx, ry, rw, rh);
        if (blockCount < 0) {
            return textclickResult(false, "", "屏幕截图失败");
        }
    }

    // 分发动作
    if (action == "get") {
        std::string all = eng.getAllText();
        TextClickEngine::setClipboardText(all);
        return textclickResult(true, all.c_str(), "文本已复制到剪贴板");
    }
    else if (action == "list") {
        return textclickListResult(eng);
    }
    else if (action == "check") {
        if (text.isEmpty()) {
            return textclickResult(false, "", "缺少 text 参数");
        }
        bool found = false;
        for (int i = 0; i < blockCount; i++) {
            if (eng.getBlockText(i).find(text.toStdString()) != std::string::npos) {
                found = true;
                break;
            }
        }
        QByteArray r = found ? "是" : "否";
        TextClickEngine::setClipboardText(r.constData());
        return textclickResult(true, r, "检查完成");
    }
    else if (action == "pos") {
        if (text.isEmpty()) {
            return textclickResult(false, "", "缺少 text 参数");
        }
        int x, y;
        if (eng.findPoint(text.toStdString(), occurrence, loc, x, y)) {
            QByteArray coord = QByteArray::number(x) + "," + QByteArray::number(y);
            TextClickEngine::setClipboardText(coord.constData());
            return textclickResult(true, coord, "坐标已复制到剪贴板");
        } else {
            return textclickResult(false, "", "未找到指定文字");
        }
    }
    else if (action == "posall") {
        QByteArray result;
        for (int i = 0; i < blockCount; i++) {
            if (i > 0) result += "\n";
            int box[8];
            eng.getBlockBox(i, box);
            result += QByteArray(eng.getBlockText(i).c_str()) + ": "
                      + QByteArray(TextClickEngine::getCoordFromBox(box, loc).c_str());
        }
        TextClickEngine::setClipboardText(result.constData());
        return textclickResult(true, result, "坐标已复制到剪贴板");
    }
    else if (action == "click" || action == "double" || action == "right" || action == "move") {
        if (text.isEmpty()) {
            return textclickResult(false, "", "缺少 text 参数");
        }
        int x, y;
        if (!eng.findPoint(text.toStdString(), occurrence, loc, x, y)) {
            return textclickResult(false, "", "未找到指定文字");
        }
        TextClickEngine::MouseAction ma = TextClickEngine::MouseAction::Move;
        QByteArray msg;
        if (action == "click")        { ma = TextClickEngine::MouseAction::Click;       msg = "已点击"; }
        else if (action == "double")  { ma = TextClickEngine::MouseAction::DoubleClick; msg = "已双击"; }
        else if (action == "right")   { ma = TextClickEngine::MouseAction::RightClick;  msg = "已右击"; }
        else                          { ma = TextClickEngine::MouseAction::Move;        msg = "已移动"; }
        TextClickEngine::performMouseAction(ma, x, y);
        QByteArray coord = QByteArray::number(x) + "," + QByteArray::number(y);
        return textclickResult(true, coord, msg + ": " + text.toUtf8() + " (" + coord + ")");
    }
    else {
        return textclickResult(false, "", "未知 action: " + action.toUtf8());
    }
}
