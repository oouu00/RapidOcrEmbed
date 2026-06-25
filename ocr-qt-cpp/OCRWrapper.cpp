// ========================================================================
// ⚠️ 注意: 此文件链接静态库 RapidOcrOnnxStatic.lib
//   该 lib 不含 libjpeg-turbo (已排除, 避免与 Qt JPEG 冲突)
//   OCR 全程用 BMP 格式, 不走 JPEG codec
//   如需修改链接库, 参见 gen_libs_pri.py 和 rename_jpeg_symbols.py
// ========================================================================

#include "OCRWrapper.h"
#include "OcrConfig.h"
#include <iostream>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QStandardPaths>

// 智能裁剪空白边缘
// 算法: 灰度化 → 高斯模糊 → 自适应阈值 → 形态学膨胀 → 找轮廓边界矩形
// 不是简单纯白检测，能处理浅灰、米黄等非纯白背景
static cv::Rect trimBlankMargins(const cv::Mat& src, int margin = 5) {
    cv::Mat gray;
    if (src.channels() == 3)
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    else
        gray = src;

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);

    cv::Mat binary;
    cv::adaptiveThreshold(blurred, binary, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 15, 4);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(15, 15));
    cv::Mat dilated;
    cv::dilate(binary, dilated, kernel, cv::Point(-1, -1), 2);

    std::vector<cv::Point> points;
    cv::findNonZero(dilated, points);
    if (points.empty()) {
        return cv::Rect(0, 0, src.cols, src.rows);
    }
    cv::Rect contentRect = cv::boundingRect(points);

    int x = std::max(0, contentRect.x - margin);
    int y = std::max(0, contentRect.y - margin);
    int w = std::min(src.cols - x, contentRect.width + 2 * margin);
    int h = std::min(src.rows - y, contentRect.height + 2 * margin);

    return cv::Rect(x, y, w, h);
}

OCRWrapper::OCRWrapper()
    : m_handle(nullptr)
{
}

OCRWrapper::~OCRWrapper() {
    destroy();
}

bool OCRWrapper::initEmbedded(int numThreads) {
    // 使用 OcrInitTableEmbedded（与测试程序一致），初始化 OCR + 表格模型
    m_handle = OcrInitTableEmbedded(numThreads);
    if (!m_handle) {
        std::cerr << "Failed to initialize OCR engine" << std::endl;
        return false;
    }
    return true;
}

bool OCRWrapper::ensureLoaded(int numThreads) {
    if (m_handle) return true;
    return initEmbedded(numThreads);
}

void OCRWrapper::unload() {
    destroy();
}

std::string OCRWrapper::detect(const std::vector<unsigned char>& imageData) {
    if (!ensureLoaded()) return "";

    OCR_PARAM ocrParam = {
        m_padding, m_maxSideLen, m_boxScoreThresh,
        m_boxThresh, m_unClipRatio, m_doAngle
    };

    int result = OcrDetectMem(m_handle, imageData.data(), (int)imageData.size(), &ocrParam);

    if (result > 0) {
        int textLen = OcrGetLen(m_handle);
        if (textLen > 0) {
            char* resultPtr = nullptr;
            OcrGetResultMem(m_handle, &resultPtr);
            if (resultPtr) {
                std::string text(resultPtr);
                free(resultPtr);
                return text;
            }
        }
    }
    return "";
}

std::vector<OcrBlock> OCRWrapper::detectWithCoords(const std::vector<unsigned char>& imageData) {
    std::vector<OcrBlock> blocks;
    if (!ensureLoaded()) return blocks;

    OCR_PARAM ocrParam = {
        m_padding, m_maxSideLen, m_boxScoreThresh,
        m_boxThresh, m_unClipRatio, m_doAngle
    };

    int blockCount = OcrDetectMemEx(m_handle, imageData.data(), (int)imageData.size(), &ocrParam);
    if (blockCount <= 0) return blocks;

    for (int i = 0; i < blockCount; i++) {
        OcrBlock block;
        char textBuf[1024] = {0};
        int textLen = OcrGetBlockText(m_handle, i, textBuf, 1024);
        if (textLen > 0) {
            block.text = std::string(textBuf);
        }
        block.score = OcrGetBlockScore(m_handle, i);
        int boxBuf[8] = {0};
        OcrGetBlockBox(m_handle, i, boxBuf);
        for (int j = 0; j < 8; j++) {
            block.box.push_back(boxBuf[j]);
        }
        if (i == 0) {
            // char dbuf[256];
            // snprintf(dbuf, sizeof(dbuf), "detectWithCoords: block[0] box=[%d,%d,%d,%d,%d,%d,%d,%d] text=%.20s",
            //     boxBuf[0], boxBuf[1], boxBuf[2], boxBuf[3], boxBuf[4], boxBuf[5], boxBuf[6], boxBuf[7], block.text.c_str());
            // FILE* f = fopen("C:\\Users\\zhuyue\\Desktop\\RapidOcrEmbed\\ocr-qt-cpp\\ocr_debug.log", "a");
            // if (f) { fprintf(f, "%s\n", dbuf); fclose(f); }
        }
        block.angle = OcrGetBlockAngle(m_handle, i);
        block.angleScore = OcrGetBlockAngleScore(m_handle, i);
        blocks.push_back(block);
    }
    return blocks;
}

TableOcrResult OCRWrapper::detectTable(const std::vector<unsigned char>& imageData) {
    TableOcrResult result;
    if (!ensureLoaded()) return result;
    // 始终设置表格模式（与测试程序一致）
    setTableMode(m_tableMode);

    // ════════════════════════════════════════════════════════════════
    // 表格专用 OCR 参数（仅影响表格模式）：
    //   • GUI 界面（MainWindow）— 在线识别/批量处理/PDF
    //   • CLI 命令行（CliRunner）— 图片/PDF 文件夹
    //   • HTTP 服务器（OcrHttpServer）— Web 上传识别
    //
    // boxScoreThresh（默认0.5，比普通OCR的0.6更低）：检测更多文本块
    // unClipRatio（默认1.5，比普通OCR的1.0更大）：文本框向外扩展更多
    // → 两者配合提高文本与表格格子的 IoU 匹配率，减少空白格子
    //
    // 通用参数 padding/maxSideLen/boxThresh/doAngle 与普通OCR共用
    // ════════════════════════════════════════════════════════════════
    OCR_PARAM ocrParam = {
        m_padding,              // padding
        m_maxSideLen,           // maxSideLen
        m_tableBoxScoreThresh,  // boxScoreThresh（表格专用）
        m_boxThresh,            // boxThresh
        m_tableUnClipRatio,     // unClipRatio（表格专用）
        m_doAngle,              // doAngle（旋转开关）
    };

    // ════════════════════════════════════════════════════════════════
    // 智能裁剪空白区域（SLANet 识别前预处理）
    // 算法: 自适应阈值 + 形态学膨胀，非纯白检测
    // 目的: 减小输入尺寸，提高 SLANet 推理效率
    // ════════════════════════════════════════════════════════════════
    cv::Mat img = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (img.empty()) return result;

    cv::Rect roi = trimBlankMargins(img);
    m_cropOffsetX = roi.x;
    m_cropOffsetY = roi.y;

    std::vector<unsigned char> processBytes;
    if (roi.x > 0 || roi.y > 0 ||
        roi.width < img.cols || roi.height < img.rows) {
        cv::Mat cropped = img(roi).clone();
        cv::imencode(".bmp", cropped, processBytes);
    } else {
        processBytes = imageData;
    }

    int ok = OcrDetectTableMem(m_handle, processBytes.data(), (int)processBytes.size(), &ocrParam);
    if (!ok) return result;

    result.structureScore = OcrGetTableStructureScore(m_handle);

    char* htmlPtr = nullptr;
    int htmlLen = OcrGetTableResultMem(m_handle, &htmlPtr);
    if (htmlPtr && htmlLen > 0) {
        result.htmlStructure = std::string(htmlPtr);
        free(htmlPtr);
    }

    int textLen = OcrGetTableOcrText(m_handle, nullptr, 0);
    if (textLen > 0) {
        std::vector<char> textBuf(textLen + 1, '\0');
        OcrGetTableOcrText(m_handle, textBuf.data(), textLen + 1);
        result.ocrText = std::string(textBuf.data());
    }

    return result;
}

void OCRWrapper::destroy() {
    if (m_handle) {
        OcrDestroy(m_handle);
        m_handle = nullptr;
    }
}

void OCRWrapper::setTableMode(int mode) {
    // 映射: Qt mode: 0=SLANet(关), 1=SLANet, 2=img2table
    //        DLL mode: 0=SLANet, 1=img2table
    // Qt mode 0 和 1 都映射到 DLL mode 0 (SLANet)
    m_tableMode = mode;
    if (m_handle) {
        int dllMode = (mode == 2) ? 1 : 0;
        OcrSetTableMode(m_handle, dllMode);
    }
}

int OCRWrapper::tableMode() const {
    return m_tableMode;
}

// ═══════════════════════════════════════════════════════════════════════
// 全局共享配置: ocr_config.json (与 exe 同目录)
// GUI / CLI / HTTP 三个入口共用, 任意修改全局生效
// ═══════════════════════════════════════════════════════════════════════

QString OCRWrapper::configPath() {
    // 优先使用 AppData 目录 (用户级别, 不依赖 exe 位置)
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        // fallback: exe 同目录
        appData = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(appData);
    return appData + "/ocr_config.json";
}

void OCRWrapper::loadConfig() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) {
        // 配置文件不存在, 用默认值并创建文件
        saveConfig();
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;

    QJsonObject o = doc.object();
    m_doAngle        = o.value("doAngle").toBool(OcrConfig::kDefaultDoAngle);
    m_padding        = o.value("padding").toInt(OcrConfig::kDefaultPadding);
    m_maxSideLen     = o.value("maxSideLen").toInt(OcrConfig::kDefaultMaxSideLen);
    m_boxScoreThresh = (float)o.value("boxScoreThresh").toDouble(OcrConfig::kDefaultBoxScoreThresh);
    m_boxThresh      = (float)o.value("boxThresh").toDouble(OcrConfig::kDefaultBoxThresh);
    m_unClipRatio    = (float)o.value("unClipRatio").toDouble(OcrConfig::kDefaultUnClipRatio);
    m_tableBoxScoreThresh = (float)o.value("tableBoxScoreThresh").toDouble(OcrConfig::kDefaultTableBoxScoreThresh);
    m_tableUnClipRatio    = (float)o.value("tableUnClipRatio").toDouble(OcrConfig::kDefaultTableUnClipRatio);

    // 同步到 OCR 引擎
    if (m_handle) {
        // doAngle 在每次 detect 时从成员变量读取, 无需额外同步
    }
}

void OCRWrapper::saveConfig() {
    QJsonObject o;
    o["doAngle"]        = m_doAngle != 0;
    o["padding"]        = m_padding;
    o["maxSideLen"]     = m_maxSideLen;
    o["boxScoreThresh"] = (double)m_boxScoreThresh;
    o["boxThresh"]      = (double)m_boxThresh;
    o["unClipRatio"]    = (double)m_unClipRatio;
    o["tableBoxScoreThresh"] = (double)m_tableBoxScoreThresh;
    o["tableUnClipRatio"]    = (double)m_tableUnClipRatio;

    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.close();
    }
}
