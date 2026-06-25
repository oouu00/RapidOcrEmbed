#ifndef TEXTCLICKCLI_H
#define TEXTCLICKCLI_H

#include "OCRWrapper.h"
#include <string>
#include <vector>

// TextClick 命令行模式入口
// 兼容 TextClick 全部参数语义 (中英文):
//   -get/-获取 -click/-单击 -double/-双击 -right/-右击 -move/-移动
//   -check/-检查 -pos/-坐标 -posall/-全部坐标 -list/-列表
//   附加: -n/-第几个 -r/-区域 -loc/-位置 -output/-输出 -h/-help
//
// ocr:  外部已初始化的 OCR 引擎 (复用, 避免重复初始化)
// args: 原始命令行参数 (UTF-8), 通常为 argv[1..], 已剔除 --tc 前缀
// 返回 0 成功, 非 0 失败
int runTextClickCli(OCRWrapper* ocr, const std::vector<std::string>& args);

// 检测参数列表中是否包含 TextClick 触发参数 (用于 main.cpp 分流)
// 触发参数: --tc 或任一 -click/-单击/-pos/-坐标/... 动作参数
bool isTextClickTrigger(const std::vector<std::string>& args);

#endif // TEXTCLICKCLI_H
