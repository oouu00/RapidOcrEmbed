#include "CliRunner.h"
#include "OCRWrapper.h"
#include "OcrJson.h"
#include "ScreenshotDialog.h"
#include "TbpuLayout.h"
#include "PdfHelper.h"
#include "PdfTextLayer.h"
#include "HtmlToXlsx.h"
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QDir>
#include <QPrinter>
#include <QPdfWriter>
#include <QPageLayout>
#include <QMarginsF>
#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
static UINT g_cfUtf8 = 0;
static UINT g_cfHtml = 0;
static void initClipboardFormats() {
    if (!g_cfUtf8) g_cfUtf8 = RegisterClipboardFormatA("UTF-8");
    if (!g_cfHtml) g_cfHtml = RegisterClipboardFormatA("HTML Format");
}
static bool copyTextToClipboardWin32(const std::string& text) {
    if (text.empty()) return false;
    initClipboardFormats();
    if (!OpenClipboard(0)) return false;
    EmptyClipboard();
    size_t len = text.size() + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) { CloseClipboard(); return false; }
    memcpy(GlobalLock(hMem), text.c_str(), len);
    GlobalUnlock(hMem);
    SetClipboardData(g_cfUtf8, hMem);
    HGLOBAL hMemAnsi = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMemAnsi) {
        memcpy(GlobalLock(hMemAnsi), text.c_str(), len);
        GlobalUnlock(hMemAnsi);
        SetClipboardData(CF_TEXT, hMemAnsi);
    }
    CloseClipboard();
    return true;
}
static bool copyHtmlToClipboardWin32(const std::string& html, const std::string& plainText) {
    if (html.empty()) return copyTextToClipboardWin32(plainText);
    initClipboardFormats();
    if (!OpenClipboard(0)) return false;
    EmptyClipboard();

    size_t htmlLen = html.size() + 1;
    HGLOBAL hHtml = GlobalAlloc(GMEM_MOVEABLE, htmlLen);
    if (hHtml) {
        memcpy(GlobalLock(hHtml), html.c_str(), htmlLen);
        GlobalUnlock(hHtml);
        SetClipboardData(g_cfHtml, hHtml);
    }

    size_t textLen = plainText.size() + 1;
    HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, textLen);
    if (hText) {
        memcpy(GlobalLock(hText), plainText.c_str(), textLen);
        GlobalUnlock(hText);
        SetClipboardData(CF_UNICODETEXT, hText);
    }

    HGLOBAL hTextAnsi = GlobalAlloc(GMEM_MOVEABLE, textLen);
    if (hTextAnsi) {
        memcpy(GlobalLock(hTextAnsi), plainText.c_str(), textLen);
        GlobalUnlock(hTextAnsi);
        SetClipboardData(CF_TEXT, hTextAnsi);
    }

    CloseClipboard();
    return hHtml != nullptr;
}
#else
static bool copyTextToClipboardWin32(const std::string& text) {
    return false;
}
static bool copyHtmlToClipboardWin32(const std::string& html, const std::string& plainText) {
    return false;
}
#endif

static FILE* g_clog = nullptr;
static void CL(const char* msg) {
    // 注释掉日志文件输出
    // if (!g_clog) g_clog = fopen("ocr_debug.log", "a");
    // if (g_clog) { fprintf(g_clog, "[cli] %s\n", msg); fflush(g_clog); }
}
#include <QFileInfo>
#include <QFile>
#include <iostream>
#include <sstream>

// 把任意格式图片转为 BMP 字节流 (绕过 libjpeg 符号冲突)
static std::vector<unsigned char> imageToBmpBytes(const QString& path) {
    QImage img;
    if (!img.load(path)) return {};
    if (img.hasAlphaChannel()) {
        QImage filled(img.size(), QImage::Format_RGB32);
        filled.fill(Qt::white);
        QPainter p(&filled);
        p.drawImage(0, 0, img);
        p.end();
        img = filled;
    }
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "BMP");
    buf.close();
    return std::vector<unsigned char>(ba.begin(), ba.end());
}

// QPixmap -> BMP 字节流 (截图用)
static std::vector<unsigned char> pixmapToBmpBytes(const QPixmap& pixmap) {
    QPixmap processed = pixmap;
    if (pixmap.hasAlpha()) {
        processed = QPixmap(pixmap.size());
        processed.fill(Qt::white);
        QPainter p(&processed);
        p.drawPixmap(0, 0, pixmap);
        p.end();
    }
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    processed.save(&buf, "BMP");
    buf.close();
    return std::vector<unsigned char>(ba.begin(), ba.end());
}

// 支持的图片扩展名
static bool isImageFile(const QString& path) {
    QString lower = path.toLower();
    return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")
        || lower.endsWith(".bmp") || lower.endsWith(".webp") || lower.endsWith(".gif")
        || lower.endsWith(".tiff") || lower.endsWith(".tif");
}

// 支持的 PDF 扩展名
static bool isPdfFile(const QString& path) {
    return path.toLower().endsWith(".pdf");
}

// 递归扫描文件夹, 返回所有图片文件和 PDF 文件 (按名称排序)
static std::vector<std::string> scanFolder(const QString& dirPath) {
    std::vector<std::string> files;
    QDir dir(dirPath);
    if (!dir.exists()) return files;

    // 先扫描子文件夹 (递归)
    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subdirs) {
        auto subFiles = scanFolder(dir.absoluteFilePath(sub));
        files.insert(files.end(), subFiles.begin(), subFiles.end());
    }

    // 再扫描当前目录的图片文件和 PDF 文件
    QStringList entries = dir.entryList(QDir::Files);
    for (const QString& entry : entries) {
        if (isImageFile(entry) || isPdfFile(entry)) {
            files.push_back(dir.absoluteFilePath(entry).toStdString());
        }
    }
    return files;
}

// 收集所有路径中的图片文件和 PDF 文件 (展开文件夹)
static std::vector<std::string> collectImageFiles(const std::vector<std::string>& paths) {
    std::vector<std::string> allFiles;
    for (const std::string& p : paths) {
        QString qpath = QString::fromStdString(p);
        QFileInfo fi(qpath);
        if (fi.isDir()) {
            auto folderFiles = scanFolder(fi.absoluteFilePath());
            allFiles.insert(allFiles.end(), folderFiles.begin(), folderFiles.end());
        } else if (fi.isFile() && (isImageFile(qpath) || isPdfFile(qpath))) {
            allFiles.push_back(fi.absoluteFilePath().toStdString());
        }
    }
    return allFiles;
}

// 识别并输出 JSON (单文件, 带文件名)
static int recognizeAndPrintFile(OCRWrapper& ocr, const std::string& filename,
                                  const std::vector<unsigned char>& bmp, bool coords,
                                  bool layout, const std::string& layoutStrategy,
                                  bool withFilename) {
    try {
        std::vector<OcrBlock> blocks = ocr.detectWithCoords(bmp);
        // 排版模式: 对 blocks 原地排序 + 写 end 分隔符
        if (layout && !blocks.empty()) {
            try {
                applyLayout(blocks, layoutStrategy);
            } catch (const std::exception& e) {
                std::cerr << "[cli] layout failed: " << e.what() << std::endl;
                // 排版失败不影响基本识别，继续输出未排版结果
            } catch (...) {
                std::cerr << "[cli] layout failed: unknown error" << std::endl;
                // 排版失败不影响基本识别，继续输出未排版结果
            }
        }
        std::string fname = withFilename ? filename : "";
        if (coords || layout) {
            std::cout << OcrJson::blocksToJson(blocks, fname) << std::endl;
        } else {
            std::cout << OcrJson::textOnlyToJson(blocks, fname) << std::endl;
        }
        std::cerr << "[cli] " << filename << ": " << blocks.size() << " blocks" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[cli] recognize failed: " << e.what() << std::endl;
        std::cout << OcrJson::fileErrorJson(filename, e.what()) << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[cli] recognize failed: unknown error" << std::endl;
        std::cout << OcrJson::fileErrorJson(filename, "unknown error") << std::endl;
        return 1;
    }
}

// 识别 PDF 文件并输出 JSON (支持多页和页码选择)
static int recognizePdfFile(OCRWrapper& ocr, const std::string& filename,
                            bool coords, bool layout, const std::string& layoutStrategy,
                            bool pdf = false, int tableMode = 0,
                            const std::string& pages = "") {
    try {
        QString qpath = QString::fromStdString(filename);
        
        // 加载 PDF
        if (!PdfHelper::load(qpath)) {
            std::cerr << "[cli] failed to load PDF: " << filename << std::endl;
            std::cout << OcrJson::fileErrorJson(filename, "cannot load PDF") << std::endl;
            return 1;
        }

        int pageCount = PdfHelper::pageCount();
        if (pageCount <= 0) {
            std::cerr << "[cli] PDF has no pages: " << filename << std::endl;
            std::cout << OcrJson::fileErrorJson(filename, "PDF has no pages") << std::endl;
            PdfHelper::close();
            return 1;
        }

        // 解析页码选择 (格式: "1,2-5,all")
        QVector<int> pageIndices;
        if (pages.empty() || pages == "all") {
            for (int i = 0; i < pageCount; i++) pageIndices.append(i);
        } else {
            // 解析页码
            QString pagesStr = QString::fromStdString(pages);
            pagesStr.replace(QString::fromUtf8("，"), ",");  // 全角逗号
            QStringList parts = pagesStr.split(",", Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                if (trimmed.contains("-")) {
                    QStringList range = trimmed.split("-", Qt::SkipEmptyParts);
                    if (range.size() == 2) {
                        int start = range[0].trimmed().toInt() - 1;
                        int end = range[1].trimmed().toInt() - 1;
                        for (int p = start; p <= end && p < pageCount; p++) {
                            if (p >= 0) pageIndices.append(p);
                        }
                    }
                } else {
                    int p = trimmed.toInt() - 1;
                    if (p >= 0 && p < pageCount) pageIndices.append(p);
                }
            }
            // 去重排序
            QVector<int> unique;
            for (int p : pageIndices) {
                if (!unique.contains(p)) unique.append(p);
            }
            pageIndices = unique;
            std::sort(pageIndices.begin(), pageIndices.end());
        }

        std::cerr << "[cli] PDF: " << filename << " (" << pageCount << " pages, selecting "
                  << pageIndices.size() << ")" << std::endl;

        // PDF 输出时收集所有页面数据 (页索引 + OCR结果)
        QVector<QPair<int, std::vector<OcrBlock>>> pdfPageBlocks;
        
        int failCount = 0;
        for (int idx : pageIndices) {
            try {
                // 渲染页面
                QPixmap pixmap = PdfHelper::renderPage(idx);
                if (pixmap.isNull()) {
                    std::cerr << "[cli] failed to render page " << (idx+1) << std::endl;
                    std::string pageName = filename + "_page" + std::to_string(idx+1);
                    std::cout << OcrJson::fileErrorJson(pageName, "cannot render PDF page") << std::endl;
                    failCount++;
                    continue;
                }

                // 转换为 BMP
                QByteArray ba;
                QBuffer buf(&ba);
                buf.open(QIODevice::WriteOnly);
                pixmap.save(&buf, "BMP");
                buf.close();
                std::vector<unsigned char> bmp(ba.begin(), ba.end());

                // 识别
                if (tableMode != 0) {
                    ocr.setTableMode(tableMode);
                    auto tableResult = ocr.detectTable(bmp);
                    std::string pageName = filename + "_page" + std::to_string(idx+1);
                    auto escapeJson = [](const std::string& s) -> std::string {
                        std::string out;
                        for (char c : s) {
                            switch (c) {
                                case '"': out += "\\\""; break;
                                case '\\': out += "\\\\"; break;
                                case '\n': out += "\\n"; break;
                                default: out += c;
                            }
                        }
                        return out;
                    };
                    std::cout << "{\"code\":0,\"file\":\"" << escapeJson(pageName)
                              << "\",\"type\":\"table\",\"html\":\"" << escapeJson(tableResult.htmlStructure)
                              << "\",\"text\":\"" << escapeJson(tableResult.ocrText)
                              << "\",\"structureScore\":" << tableResult.structureScore
                              << "}" << std::endl;
                } else {
                    std::vector<OcrBlock> blocks = ocr.detectWithCoords(bmp);
                    if (layout && !blocks.empty()) {
                        try {
                            applyLayout(blocks, layoutStrategy);
                        } catch (...) {}
                    }
                    std::string pageName = filename + "_page" + std::to_string(idx+1);
                    if (coords || layout) {
                        std::cout << OcrJson::blocksToJson(blocks, pageName) << std::endl;
                    } else {
                        std::cout << OcrJson::textOnlyToJson(blocks, pageName) << std::endl;
                    }
                    if (pdf) {
                        pdfPageBlocks.append(qMakePair(idx, blocks));
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[cli] page " << (idx+1) << " failed: " << e.what() << std::endl;
                std::string pageName = filename + "_page" + std::to_string(idx+1);
                std::cout << OcrJson::fileErrorJson(pageName, e.what()) << std::endl;
                failCount++;
            } catch (...) {
                std::cerr << "[cli] page " << (idx+1) << " failed: unknown error" << std::endl;
                std::string pageName = filename + "_page" + std::to_string(idx+1);
                std::cout << OcrJson::fileErrorJson(pageName, "unknown error") << std::endl;
                failCount++;
            }
        }

        PdfHelper::close();
        
        // 生成可搜索 PDF (修改原始PDF，添加不可见文本层)
        if (pdf && !pdfPageBlocks.isEmpty()) {
            try {
                QString pdfPath = QFileInfo(qpath).absolutePath() + "/" + 
                                  QFileInfo(qpath).completeBaseName() + "_ocr.pdf";
                if (PdfTextLayer::modifyPdfWithOcrText(qpath, pdfPath, pdfPageBlocks, 150)) {
                    std::cerr << "[cli] wrote searchable pdf: " << pdfPath.toStdString() << std::endl;
                } else {
                    std::cerr << "[cli] failed to build searchable PDF" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "[cli] PDF generation failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[cli] PDF generation failed: unknown error" << std::endl;
            }
        }
        
        return failCount > 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "[cli] PDF processing failed: " << e.what() << std::endl;
        std::cout << OcrJson::fileErrorJson(filename, e.what()) << std::endl;
        PdfHelper::close();
        return 1;
    } catch (...) {
        std::cerr << "[cli] PDF processing failed: unknown error" << std::endl;
        std::cout << OcrJson::fileErrorJson(filename, "unknown error") << std::endl;
        PdfHelper::close();
        return 1;
    }
}

int runCliFile(const std::string& imagePath, bool coords,
               bool layout, const std::string& layoutStrategy, bool pdf, int tableMode,
               const std::string& pages, bool clipboard, bool doAngle) {
    try {
        CL("runCliFile: start");
        QString qpath = QString::fromStdString(imagePath);
        
        // PDF 文件: 走 PDF 处理路径
        if (isPdfFile(qpath)) {
            CL("PDF file detected, calling recognizePdfFile");
            OCRWrapper ocr;
            if (!ocr.initEmbedded(4)) {
                std::cout << R"({"code":2,"msg":"OCR init failed"})" << std::endl;
                return 2;
            }
            ocr.loadConfig();
            if (!doAngle) ocr.setDoAngle(0); // --rotate=0 覆盖配置
            return recognizePdfFile(ocr, imagePath, coords, layout, layoutStrategy, pdf, tableMode, pages);
        }
        
        CL("loading image...");
        QImage img;
        if (!img.load(qpath)) {
            CL("failed to load image");
            std::cout << R"({"code":1,"msg":"cannot load image"})" << std::endl;
            return 1;
        }
        CL("image loaded OK");

        CL("converting to BMP...");
        if (img.hasAlphaChannel()) {
            QImage filled(img.size(), QImage::Format_RGB32);
            filled.fill(Qt::white);
            QPainter p(&filled);
            p.drawImage(0, 0, img);
            p.end();
            img = filled;
        }
        QByteArray bmpBytes;
        QBuffer bmpBuf(&bmpBytes);
        bmpBuf.open(QIODevice::WriteOnly);
        img.save(&bmpBuf, "BMP");
        bmpBuf.close();
        std::vector<unsigned char> bmp(bmpBytes.begin(), bmpBytes.end());
        CL("BMP converted");

        CL("calling ocr.initEmbedded...");
        OCRWrapper ocr;
        if (!ocr.initEmbedded(4)) {
            CL("OCR init FAILED");
            std::cout << R"({"code":2,"msg":"OCR init failed"})" << std::endl;
            return 2;
        }
        ocr.loadConfig();
        if (!doAngle) ocr.setDoAngle(0); // --rotate=0 覆盖配置
        CL("OCR init OK");

        if (tableMode != 0) {
            CL("table mode: calling detectTable...");
            ocr.setTableMode(tableMode);
            auto tableResult = ocr.detectTable(bmp);
            CL("detectTable done");
            
            // 输出 JSON: 包含 html 和 text
            std::string html = tableResult.htmlStructure;
            std::string text = tableResult.ocrText;
            float score = tableResult.structureScore;
            
            // 转义 JSON 字符串
            auto escapeJson = [](const std::string& s) -> std::string {
                std::string out;
                for (char c : s) {
                    switch (c) {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        default: out += c;
                    }
                }
                return out;
            };
            
            std::string escapedText = escapeJson(text);
            std::string escapedHtml = escapeJson(html);
            std::cout << "{\"code\":0,\"type\":\"table\",\"text\":\""
                      << escapedText
                      << "\",\"html\":\""
                      << escapedHtml
                      << "\",\"structureScore\":" << score
                      << "}" << std::endl;
            
            // 输出 HTML 文件
            std::string htmlPath = QFileInfo(qpath).absolutePath().toStdString()
                                 + "/" + QFileInfo(qpath).completeBaseName().toStdString() + "_table.html";
            std::ofstream htmlFile(htmlPath);
            if (htmlFile.is_open()) {
                // 如果 html 已经是完整 HTML 文档 (wrapFullHtml 输出), 直接写入, 避免双重包装
                if (html.find("<!DOCTYPE") == 0 || html.find("<html") == 0) {
                    htmlFile << html;
                } else {
                    htmlFile << "<html><head><meta charset=\"UTF-8\">"
                             << "<style>table{border-collapse:collapse;width:100%}"
                             << "td,th{border:1px solid black;padding:8px;text-align:left}</style>"
                             << "</head><body>" << html << "</body></html>";
                }
                htmlFile.close();
                std::cerr << "[cli] table HTML: " << htmlPath << std::endl;
            }
            
            // 输出 XLSX 文件
            std::string xlsxPath = QFileInfo(qpath).absolutePath().toStdString()
                                 + "/" + QFileInfo(qpath).completeBaseName().toStdString() + "_table.xlsx";
            if (htmlToXlsx(html, xlsxPath)) {
                std::cerr << "[cli] table XLSX: " << xlsxPath << std::endl;
            }
            
            if (clipboard) {
                copyHtmlToClipboardWin32(html, text);
                std::cerr << "[cli] table copied to clipboard (HTML)" << std::endl;
                return 0;
            }

            std::cerr << "[cli] table mode done, score=" << score << std::endl;
            return 0;
        }

        CL("calling ocr.detect...");
        std::vector<OcrBlock> blocks = ocr.detectWithCoords(bmp);
        CL("detect done");
        // 排版模式: 对 blocks 原地排序 + 写 end 分隔符
        if (layout && !blocks.empty()) {
            try {
                applyLayout(blocks, layoutStrategy);
            } catch (const std::exception& e) {
                std::cerr << "[cli] layout failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[cli] layout failed: unknown error" << std::endl;
            }
        }
        if (clipboard) {
            std::string allText;
            for (const auto& b : blocks) {
                allText += b.text;
                allText += b.end;
            }
            copyTextToClipboardWin32(allText);
            std::cerr << "[cli] text copied to clipboard (" << allText.size() << " bytes)" << std::endl;
            return 0;
        }
        if (coords || layout) {
            std::cout << OcrJson::blocksToJson(blocks) << std::endl;
        } else {
            std::cout << OcrJson::textOnlyToJson(blocks) << std::endl;
        }
        std::cerr << "[cli] done, " << blocks.size() << " blocks" << std::endl;
        
        // 生成可搜索 PDF: Qt 转 PDF → modifyPdfWithOcrText 加文字层
        if (pdf && !blocks.empty()) {
            try {
                QString pdfPath = QFileInfo(qpath).absolutePath() + "/" +
                                  QFileInfo(qpath).completeBaseName() + ".pdf";
                QString tmpPdfPath = pdfPath + ".qt.pdf";

                // 用 QPdfWriter 把图片写入 PDF (保持原始尺寸)
                {
                    QPdfWriter writer(tmpPdfPath);
                    QPageLayout layout(QPageSize(QSizeF(img.width(), img.height()), QPageSize::Point),
                                       QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
                    writer.setPageLayout(layout);
                    writer.setResolution(72);
                    QPainter painter(&writer);
                    painter.drawImage(0, 0, img);
                    painter.end();
                }

                // OCR 坐标用图片像素, dpi=72 (QPdfWriter 用 72dpi 的 point 坐标)
                QVector<QPair<int, std::vector<OcrBlock>>> pdfPageBlocks;
                pdfPageBlocks.append(qMakePair(0, blocks));
                if (PdfTextLayer::modifyPdfWithOcrText(tmpPdfPath, pdfPath, pdfPageBlocks, 72)) {
                    std::cerr << "[cli] wrote searchable pdf: " << pdfPath.toStdString() << std::endl;
                } else {
                    std::cerr << "[cli] modifyPdfWithOcrText failed, using raw pdf" << std::endl;
                    QFile::rename(tmpPdfPath, pdfPath);
                }
                QFile::remove(tmpPdfPath);
            } catch (const std::exception& e) {
                std::cerr << "[cli] PDF generation failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[cli] PDF generation failed: unknown error" << std::endl;
            }
        }
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[cli] processing failed: " << e.what() << std::endl;
        std::cout << OcrJson::fileErrorJson(imagePath, e.what()) << std::endl;
        return 1;
    } catch (...) {
        std::cout << OcrJson::fileErrorJson(imagePath, "unknown error") << std::endl;
        return 1;
    }
}

int runCliFiles(const std::vector<std::string>& paths, bool coords,
                bool layout, const std::string& layoutStrategy, bool pdf, int tableMode,
                bool clipboard, bool doAngle) {
    std::cerr << "[cli] mode: " << (coords ? "coords" : "text")
              << (layout ? " + layout(" + layoutStrategy + ")" : "")
              << (pdf ? " + pdf" : "") << " (multi-file)" << std::endl;

    // 收集所有图片文件和 PDF 文件
    std::vector<std::string> files = collectImageFiles(paths);
    if (files.empty()) {
        std::cerr << "[cli] no image or PDF files found" << std::endl;
        std::cout << R"({"code":1,"msg":"no image or PDF files found"})" << std::endl;
        return 1;
    }
    std::cerr << "[cli] found " << files.size() << " file(s)" << std::endl;

    // 初始化 OCR 引擎 (一次)
    std::cerr << "[cli] initializing OCR engine..." << std::endl;
    OCRWrapper ocr;
    if (!ocr.initEmbedded(4)) {
        std::cerr << "[cli] OCR init failed" << std::endl;
        std::cout << R"({"code":2,"msg":"OCR init failed"})" << std::endl;
        return 2;
    }
    ocr.loadConfig();
    if (!doAngle) ocr.setDoAngle(0); // --rotate=0 覆盖配置

    bool withFilename = (files.size() > 1);
    int failCount = 0;

    for (size_t i = 0; i < files.size(); i++) {
        const std::string& filePath = files[i];
        std::cerr << "[cli] (" << (i + 1) << "/" << files.size() << ") " << filePath << std::endl;

        QString qpath = QString::fromStdString(filePath);
        
        // 检查是否是 PDF 文件
        if (isPdfFile(qpath)) {
            int result = recognizePdfFile(ocr, filePath, coords, layout, layoutStrategy, pdf, tableMode, "");
            if (result != 0) {
                failCount++;
            }
            continue;
        }
        
        // 普通图片文件
        QImage img;
        if (!img.load(qpath)) {
            std::cout << OcrJson::fileErrorJson(filePath, "cannot load image") << std::endl;
            failCount++;
            continue;
        }
        
        if (tableMode != 0) {
            // 表格模式
            if (img.hasAlphaChannel()) {
                QImage filled(img.size(), QImage::Format_RGB32);
                filled.fill(Qt::white);
                QPainter p(&filled);
                p.drawImage(0, 0, img);
                p.end();
                img = filled;
            }
            QByteArray bmpBytes;
            QBuffer bmpBuf(&bmpBytes);
            bmpBuf.open(QIODevice::WriteOnly);
            img.save(&bmpBuf, "BMP");
            bmpBuf.close();
            std::vector<unsigned char> bmp(bmpBytes.begin(), bmpBytes.end());
            
            ocr.setTableMode(tableMode);
            auto tableResult = ocr.detectTable(bmp);
            std::string html = tableResult.htmlStructure;
            std::string text = tableResult.ocrText;
            float score = tableResult.structureScore;

            // 输出 HTML 文件
            std::string htmlPath = QFileInfo(qpath).absolutePath().toStdString()
                                 + "/" + QFileInfo(qpath).completeBaseName().toStdString() + "_table.html";
            std::ofstream htmlFile(htmlPath);
            if (htmlFile.is_open()) {
                if (html.find("<!DOCTYPE") == 0 || html.find("<html") == 0) {
                    htmlFile << html;
                } else {
                    htmlFile << "<html><head><meta charset=\"UTF-8\">"
                             << "<style>table{border-collapse:collapse;width:100%}"
                             << "td,th{border:1px solid black;padding:8px;text-align:left}</style>"
                             << "</head><body>" << html << "</body></html>";
                }
                htmlFile.close();
                std::cerr << "[cli] table HTML: " << htmlPath << std::endl;
            }

            // 输出 XLSX 文件
            std::string xlsxPath = QFileInfo(qpath).absolutePath().toStdString()
                                 + "/" + QFileInfo(qpath).completeBaseName().toStdString() + "_table.xlsx";
            if (htmlToXlsx(html, xlsxPath)) {
                std::cerr << "[cli] table XLSX: " << xlsxPath << std::endl;
            }
            
            // 输出 JSON
            auto escapeJson = [](const std::string& s) -> std::string {
                std::string out;
                for (char c : s) {
                    switch (c) {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        default: out += c;
                    }
                }
                return out;
            };
            
            if (clipboard) {
                copyHtmlToClipboardWin32(html, text);
                std::cerr << "[cli] table copied to clipboard (HTML)" << std::endl;
                continue;
            }
            std::cout << "{\"code\":0,\"file\":\"" << escapeJson(filePath)
                      << "\",\"type\":\"table\",\"text\":\"" << escapeJson(text)
                      << "\",\"html\":\"" << escapeJson(html)
                      << "\",\"structureScore\":" << score
                      << "}" << std::endl;
            continue;
        }
        
        // 保存原始图像 (用于 PDF 输出)
        QImage imgForPdf;
        if (pdf) {
            imgForPdf = img;
        }
        
        // 转换为 BMP (填白底)
        if (img.hasAlphaChannel()) {
            QImage filled(img.size(), QImage::Format_RGB32);
            filled.fill(Qt::white);
            QPainter p(&filled);
            p.drawImage(0, 0, img);
            p.end();
            img = filled;
        }
        QByteArray bmpBytes;
        QBuffer bmpBuf(&bmpBytes);
        bmpBuf.open(QIODevice::WriteOnly);
        img.save(&bmpBuf, "BMP");
        bmpBuf.close();
        std::vector<unsigned char> bmp(bmpBytes.begin(), bmpBytes.end());
        
        // 识别
        std::vector<OcrBlock> blocks = ocr.detectWithCoords(bmp);
        if (layout && !blocks.empty()) {
            applyLayout(blocks, layoutStrategy);
        }
        
        if (clipboard) {
            std::string allText;
            for (const auto& b : blocks) {
                allText += b.text;
                allText += b.end;
            }
            copyTextToClipboardWin32(allText);
            std::cerr << "[cli] text copied to clipboard (" << allText.size() << " bytes)" << std::endl;
            continue;
        }
        // 输出 JSON
        std::string fname = withFilename ? filePath : "";
        if (coords || layout) {
            std::cout << OcrJson::blocksToJson(blocks, fname) << std::endl;
        } else {
            std::cout << OcrJson::textOnlyToJson(blocks, fname) << std::endl;
        }
        std::cerr << "[cli] " << filePath << ": " << blocks.size() << " blocks" << std::endl;
        
        // 生成可搜索 PDF: Qt 转 PDF → modifyPdfWithOcrText 加文字层
        if (pdf && !blocks.empty()) {
            try {
                QString pdfPath = QFileInfo(qpath).absolutePath() + "/" +
                                  QFileInfo(qpath).completeBaseName() + ".pdf";
                QString tmpPdfPath = pdfPath + ".qt.pdf";
                {
                    QPdfWriter writer(tmpPdfPath);
                    QPageLayout layout(QPageSize(QSizeF(imgForPdf.width(), imgForPdf.height()), QPageSize::Point),
                                       QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
                    writer.setPageLayout(layout);
                    writer.setResolution(72);
                    QPainter painter(&writer);
                    painter.drawImage(0, 0, imgForPdf);
                    painter.end();
                }
                QVector<QPair<int, std::vector<OcrBlock>>> pdfPageBlocks;
                pdfPageBlocks.append(qMakePair(0, blocks));
                if (PdfTextLayer::modifyPdfWithOcrText(tmpPdfPath, pdfPath, pdfPageBlocks, 72)) {
                    std::cerr << "[cli] wrote searchable pdf: " << pdfPath.toStdString() << std::endl;
                } else {
                    std::cerr << "[cli] modifyPdfWithOcrText failed, using raw pdf" << std::endl;
                    QFile::rename(tmpPdfPath, pdfPath);
                }
                QFile::remove(tmpPdfPath);
            } catch (const std::exception& e) {
                std::cerr << "[cli] PDF generation failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[cli] PDF generation failed: unknown error" << std::endl;
            }
        }
    }

    std::cerr << "[cli] done, " << files.size() << " file(s), " << failCount << " failed" << std::endl;
    return failCount > 0 ? 1 : 0;
}

int runCliShot(bool coords, bool layout, const std::string& layoutStrategy, const std::string& region, bool clipboard, bool doAngle) {
    std::cerr << "[cli] mode: " << (coords ? "coords" : "text")
              << (layout ? " + layout(" + layoutStrategy + ")" : "") << std::endl;

    // 抓全屏
    QScreen* screen = QGuiApplication::primaryScreen();
    QPixmap fullScreen = screen->grabWindow(0);
    if (fullScreen.isNull()) {
        std::cerr << "[cli] failed to grab screen" << std::endl;
        std::cout << R"({"code":3,"msg":"cannot grab screen"})" << std::endl;
        return 3;
    }

    QPixmap cropped;

    // 如果指定了区域参数，直接裁剪
    if (!region.empty()) {
        std::cerr << "[cli] using region: " << region << std::endl;
        // 解析区域参数 "x,y,w,h"
        std::vector<int> coords;
        std::stringstream ss(region);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try {
                coords.push_back(std::stoi(item));
            } catch (...) {
                std::cerr << "[cli] invalid region format: " << region << std::endl;
                std::cout << "{\"code\":5,\"msg\":\"invalid region format, expected x,y,w,h\"}" << std::endl;
                return 5;
            }
        }
        if (coords.size() != 4) {
            std::cerr << "[cli] region needs 4 values (x,y,w,h), got " << coords.size() << std::endl;
            std::cout << "{\"code\":5,\"msg\":\"region needs 4 values (x,y,w,h)\"}" << std::endl;
            return 5;
        }
        int x = coords[0], y = coords[1], w = coords[2], h = coords[3];
        if (x < 0 || y < 0 || w <= 0 || h <= 0) {
            std::cerr << "[cli] invalid region values: x=" << x << ", y=" << y << ", w=" << w << ", h=" << h << std::endl;
            std::cout << "{\"code\":5,\"msg\":\"invalid region values\"}" << std::endl;
            return 5;
        }
        cropped = fullScreen.copy(x, y, w, h);
        std::cerr << "[cli] captured region: " << cropped.width() << "x" << cropped.height() << std::endl;
    } else {
        // 弹出截图框, 等待用户框选
        std::cerr << "[cli] screenshot mode, please select screen region..." << std::endl;
        ScreenshotDialog dlg(fullScreen, false);
        QEventLoop loop;
        QObject::connect(&dlg, &ScreenshotDialog::closed, [&](const QPixmap& pix) {
            cropped = pix;
            loop.quit();
        });
        dlg.showFullScreen();
        loop.exec();  // 阻塞直到用户框选完成

        if (cropped.isNull()) {
            std::cerr << "[cli] screenshot cancelled" << std::endl;
            std::cout << R"({"code":4,"msg":"screenshot cancelled"})" << std::endl;
            return 4;
        }
        std::cerr << "[cli] captured region: " << cropped.width() << "x" << cropped.height() << std::endl;
    }

    // 初始化 OCR
    std::cerr << "[cli] initializing OCR engine..." << std::endl;
    OCRWrapper ocr;
    if (!ocr.initEmbedded(4)) {
        std::cerr << "[cli] OCR init failed" << std::endl;
        std::cout << R"({"code":2,"msg":"OCR init failed"})" << std::endl;
        return 2;
    }
    ocr.loadConfig();
    if (!doAngle) ocr.setDoAngle(0); // --rotate=0 覆盖配置

    std::vector<unsigned char> bmp = pixmapToBmpBytes(cropped);
    std::cerr << "[cli] recognizing..." << std::endl;
    std::vector<OcrBlock> blocks = ocr.detectWithCoords(bmp);
    // 排版模式: 对 blocks 原地排序 + 写 end 分隔符
    if (layout && !blocks.empty()) {
        applyLayout(blocks, layoutStrategy);
    }
    if (clipboard) {
        std::string allText;
        for (const auto& b : blocks) {
            allText += b.text;
            allText += b.end;
        }
        copyTextToClipboardWin32(allText);
        std::cerr << "[cli] text copied to clipboard (" << allText.size() << " bytes)" << std::endl;
        return 0;
    }
    if (coords || layout) {
        std::cout << OcrJson::blocksToJson(blocks) << std::endl;
        std::cerr << "[cli] done, " << blocks.size() << " blocks (coords/layout mode)" << std::endl;
    } else {
        std::cout << OcrJson::textOnlyToJson(blocks) << std::endl;
        std::cerr << "[cli] done, " << blocks.size() << " blocks (text mode)" << std::endl;
    }
    return 0;
}
