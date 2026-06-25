// This file contains TBPU layout algorithm code ported from Umi-OCR
// Project: https://github.com/hiroi-sora/Umi-OCR
// Copyright (c) 2023 hiroi-sora
// Released under the MIT License
// 
// 本算法混合了 Umi-OCR 的 GapTree 多栏版面分析算法和 k2pdfopt 的精细排序算法
// k2pdfopt 项目: http://willus.com/k2pdfopt/
// 两者均为开源软件, 符合开源合规要求
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

#ifndef TBPULAYOUT_H
#define TBPULAYOUT_H

#include "OCRWrapper.h"
#include <string>
#include <vector>

// ============================================================
// TBPU (Text Block Processing Unit) 文本块处理单元
// 混合了 Umi-OCR 的 GapTree 多栏版面分析算法和 k2pdfopt 的精细排序算法
// Umi-OCR 项目: https://github.com/hiroi-sora/Umi-OCR
// k2pdfopt 项目: http://willus.com/k2pdfopt/
// 两者均为开源软件, 符合开源合规要求
//
// 核心:
//   line_preprocessing : 旋转校正 (中位数角度) + bbox 标准化 + 空文本过滤 + 按 y 排序
//   GapTree            : 间隙树排序算法 (多栏版面分析核心)
//   FineSort           : 精细排序算法 (解决复杂布局中的文本块排序问题)
//   ParagraphParse     : 段落分析器 (自然段合并)
//   SingleLine         : 单栏同行合并
//   8 种排版策略 + 工厂 (未知 key 回退 "none")
//
// 对外接口: applyLayout(blocks, strategyKey)
// 排版后 blocks 被原地重排, 每块的 end 字段写为结尾分隔符 ('' / ' ' / '\n')。
// ============================================================

// 旋转角度阈值 (度), 低于此角度不执行旋转校正
extern const double TBPU_ANGLE_THRESHOLD;
// 段落对齐/行间距对比阈值 (×行高)
extern const double TBPU_TH;

// 排版策略信息 (供 GUI 下拉框/帮助使用)
struct LayoutStrategyInfo {
    const char* key;
    const char* label;
    const char* description;
};

// 获取所有排版策略信息
const std::vector<LayoutStrategyInfo>& layoutStrategies();

// 应用排版策略
// blocks: 输入输出参数, 会被原地修改 (排序、合并、设置 end 字段)
// strategyKey: 策略 key (如 "multi_para", "single_code", "none")
void applyLayout(std::vector<OcrBlock>& blocks, const std::string& strategyKey);

#endif // TBPULAYOUT_H