#ifndef OCRWRAPPER_H
#define OCRWRAPPER_H

#include <string>
#include <vector>
#include <memory>
#include <QString>

// 直接静态链接 RapidOcrEmbed 的 C API
#include "OcrLiteCApi.h"

// OpenCV (用于 SLANet 识别前的智能空白裁剪)
#include <opencv2/opencv.hpp>

// OCR 参数结构体
// ═══════════════════════════════════════════════════════════════════════
// 【重要设计说明】OCR 参数分两套，互不影响：
//
// 1. 普通 OCR 参数（boxScoreThresh=0.6, unClipRatio=1.0）
//    → 用于 普通 OCR 识别（detect / detectWithCoords）
//    → GUI 在线识别、CLI 命令行、HTTP 服务器的普通模式
//
// 2. 表格 OCR 参数（boxScoreThresh=0.5, unClipRatio=1.5）
//    → 用于 表格模式（detectTable）
//    → 更低阈值提高文本与格子的 IoU 匹配率
//
// 两套参数通过 ocr_config.json 分别存储，高级设置弹窗中分别配置
// ═══════════════════════════════════════════════════════════════════════
struct OcrParam {
    int padding = 50;
    int maxSideLen = 1024;
    float boxScoreThresh = 0.6f;
    float boxThresh = 0.3f;
    float unClipRatio = 1.0f;
    int doAngle = 1;
};

// OCR 结果块
struct OcrBlock {
    std::string text;
    float score;
    std::vector<int> box;
    int angle;
    float angleScore;
    std::string end = "\n";
};

// 表格识别结果
struct TableOcrResult {
    std::string htmlStructure;
    std::string ocrText;
    float structureScore = 0;
};

// 静态直调版本
class OCRWrapper {
public:
    OCRWrapper();
    ~OCRWrapper();

    bool initEmbedded(int numThreads = 4);
    bool ensureLoaded(int numThreads = 4);  // 确保已加载, 未加载则重新初始化
    void unload();                           // 释放模型, 省内存
    std::string detect(const std::vector<unsigned char>& imageData);
    std::vector<OcrBlock> detectWithCoords(const std::vector<unsigned char>& imageData);
    TableOcrResult detectTable(const std::vector<unsigned char>& imageData);
    void setTableMode(int mode);  // 0=关, 1=SLANet, 2=img2table (纯OpenCV, 无需模型)
    int tableMode() const;
    void setDoAngle(int v) { m_doAngle = v; }
    int doAngle() const { return m_doAngle; }

    void setPadding(int v) { m_padding = v; }
    int padding() const { return m_padding; }
    void setMaxSideLen(int v) { m_maxSideLen = v; }
    int maxSideLen() const { return m_maxSideLen; }
    void setBoxScoreThresh(float v) { m_boxScoreThresh = v; }
    float boxScoreThresh() const { return m_boxScoreThresh; }
    void setBoxThresh(float v) { m_boxThresh = v; }
    float boxThresh() const { return m_boxThresh; }
    void setUnClipRatio(float v) { m_unClipRatio = v; }
    float unClipRatio() const { return m_unClipRatio; }

    void setTableBoxScoreThresh(float v) { m_tableBoxScoreThresh = v; }
    float tableBoxScoreThresh() const { return m_tableBoxScoreThresh; }
    void setTableUnClipRatio(float v) { m_tableUnClipRatio = v; }
    float tableUnClipRatio() const { return m_tableUnClipRatio; }

    // 全局共享配置: 用户目录的 ocr_config.json
    // GUI/CLI/HTTP 共用, 任意修改全局生效
    static QString configPath();
    void loadConfig();   // 从 ocr_config.json 读取并应用参数
    void saveConfig();   // 将当前参数写入 ocr_config.json

    void destroy();

    bool isInitialized() const { return m_handle != nullptr; }

private:
    int m_tableMode = 0;
    int m_doAngle = 1;
    int m_padding = 50;
    int m_maxSideLen = 1024;
    float m_boxScoreThresh = 0.6f;
    float m_boxThresh = 0.3f;
    float m_unClipRatio = 1.0f;
    float m_tableBoxScoreThresh = 0.5f;
    float m_tableUnClipRatio = 1.5f;
    OCR_HANDLE m_handle;
    int m_cropOffsetX = 0;
    int m_cropOffsetY = 0;
};

#endif // OCRWRAPPER_H
