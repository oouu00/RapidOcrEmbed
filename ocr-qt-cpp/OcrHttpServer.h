#ifndef OCRHTTPSERVER_H
#define OCRHTTPSERVER_H

#include <QObject>
#include <QHash>
#include <QSocketNotifier>
#include <functional>

// 前向声明: 不在此处 include winsock2.h / windows.h
// 避免与项目中其他已 include windows.h 的文件产生重复定义冲突
// winsock2.h 必须在 windows.h 之前, 统一在 .cpp 中处理
// OcrHttpServer.cpp 保证在 #include "OcrHttpServer.h" 之前已 include winsock2.h
#ifndef _WINSOCK2API_
typedef uintptr_t SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#endif

class OCRWrapper;

// 轻量 HTTP 服务 (基于原生 Winsock + QSocketNotifier, 不依赖 Qt Network)
// 避免全静态链接时 Qt Network 与 ORT/OpenCV 的 /FORCE:MULTIPLE 堆符号冲突
// OCR 引擎由外部传入 (不自己 new/destroy), 与 GUI 共享同一实例。
// 路由:
//   GET  /          返回内嵌的浏览器界面 (webui.h)
//   POST /ocr       multipart/form-data 上传图片 (字段名 file), 返回 JSON
//   POST /ocr-raw   body 直接是图片二进制字节, 返回 JSON (方便 curl 调用)
class OcrHttpServer : public QObject {
    Q_OBJECT
public:
    // ocr: 外部传入的 OCR 引擎实例 (生命周期由调用方管理)
    // 端口 port
    explicit OcrHttpServer(quint16 port, OCRWrapper* ocr, QObject* parent = nullptr);
    ~OcrHttpServer();

    bool start();  // 返回是否成功监听
    quint16 port() const;
    void setOcrActivityCallback(std::function<void()> cb);  // OCR使用时回调 (重置空闲计时器)
    void setClipboardCallback(std::function<void(const QString&)> cb);  // 剪贴板回调

private slots:
    void onListenReady();
    void onClientReady(int socketFd);

private:
    // 关闭客户端连接
    void closeClient(SOCKET s);
    // 处理已读取完整的请求
    void handleRequest(SOCKET socket, const QByteArray& method,
                       const QByteArray& path, const QByteArray& body,
                       const QHash<QString, QString>& headers,
                       const QByteArray& clientIp);
    // 把图片字节转 BMP 再交给 OCR 识别, 返回 JSON
    // coords=true 返回带坐标的 blocks, false 仅返回 text
    // layout=true 启用排版模式; layoutStrategy 排版策略 key
    // pdf=true 时在 JSON 里附加 "pdf" 字段 (base64 可搜索 PDF)
    QByteArray recognizeImage(const QByteArray& imageBytes, bool coords,
                              bool layout = false, const std::string& layoutStrategy = "multi_para",
                              bool pdf = false, int tableMode = 0, bool doAngle = true);
    // 识别图片字节并附带文件名 (多文件模式)
    QByteArray recognizeImageWithName(const QByteArray& imageBytes, const QByteArray& filename,
                                      bool coords, bool layout = false,
                                      const std::string& layoutStrategy = "multi_para",
                                      bool pdf = false, int tableMode = 0, bool doAngle = true);
    // 处理 TextClick 风格 JSON 请求 (POST /textclick), 返回 TextClick 风格 JSON
    QByteArray handleTextClick(const QByteArray& jsonBody);
    // 解析 multipart/form-data, 提取第一个文件部分
    static QByteArray extractMultipartFile(const QByteArray& body, const QByteArray& contentType);
    // 解析 multipart/form-data, 提取所有文件部分 (返回 filename+data 对)
    static QVector<QPair<QByteArray, QByteArray>> extractAllMultipartFiles(
        const QByteArray& body, const QByteArray& contentType);
    // 从 URL path 提取查询参数 (如 /ocr?mode=coords -> mode=coords)
    static QHash<QString, QString> parseQuery(const QByteArray& pathWithQuery);
    // 发送 HTTP 响应
    static void sendResponse(SOCKET socket, int code, const QByteArray& contentType,
                             const QByteArray& body);
    // 在主线程执行截图框选, 返回裁剪的 QPixmap (阻塞当前线程直到用户框选完成)
    QPixmap doScreenshotOnMainThread();

    SOCKET m_listenSocket;
    QSocketNotifier* m_listenNotifier;
    OCRWrapper* m_ocr;   // 外部传入, 不拥有
    std::function<void()> m_onOcrActivity;  // OCR活动回调
    std::function<void(const QString&)> m_onClipboardCopy;  // 剪贴板回调
    quint16 m_port;
    // 每个连接的缓冲 (可能分多次到达)
    struct ConnBuf {
        QByteArray header;   // 已收的请求头部分
        QByteArray body;     // 已收的 body 部分
        bool headerParsed = false;
        int contentLength = 0;
        QByteArray method;
        QByteArray path;
        QHash<QString, QString> headers;
        QByteArray clientIp; // 客户端 IP (用于本地限制)
    };
    QHash<SOCKET, ConnBuf> m_bufs;
    QHash<SOCKET, QSocketNotifier*> m_notifiers;
};

#endif // OCRHTTPSERVER_H
