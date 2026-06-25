#ifndef OCRCONFIG_H
#define OCRCONFIG_H

// OCR 全局配置的默认值和配置文件路径
// 实际 load/save 实现在 OCRWrapper 中, 此头文件只定义常量

namespace OcrConfig {
    // 普通模式默认值
    constexpr bool   kDefaultDoAngle        = false;
    constexpr int    kDefaultPadding         = 50;
    constexpr int    kDefaultMaxSideLen      = 1024;
    constexpr double kDefaultBoxScoreThresh  = 0.6;
    constexpr double kDefaultBoxThresh       = 0.3;
    constexpr double kDefaultUnClipRatio     = 1.0;
    // 表格模式默认值
    constexpr double kDefaultTableBoxScoreThresh = 0.5;
    constexpr double kDefaultTableUnClipRatio    = 1.5;
}

#endif // OCRCONFIG_H
