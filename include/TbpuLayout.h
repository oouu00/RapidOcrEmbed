#ifndef __TBPULAYOUT_H__
#define __TBPULAYOUT_H__

#include "OcrStruct.h"
#include <string>
#include <vector>

// 排版策略常量
#define LAYOUT_MULTI_PARA   "multi_para"
#define LAYOUT_MULTI_LINE   "multi_line"
#define LAYOUT_MULTI_NONE   "multi_none"
#define LAYOUT_SINGLE_PARA  "single_para"
#define LAYOUT_SINGLE_LINE  "single_line"
#define LAYOUT_SINGLE_NONE  "single_none"
#define LAYOUT_SINGLE_CODE  "single_code"

// 排版策略信息
struct LayoutStrategyInfo {
    const char* key;
    const char* label;
    const char* description;
};

// 获取所有排版策略信息
const std::vector<LayoutStrategyInfo>& layoutStrategies();

// 获取策略总数
int layoutStrategyCount();

// 获取策略索引 (返回 -1 表示未找到)
int layoutStrategyIndex(const std::string& key);

// 获取策略 key (返回空字符串表示越界)
const char* layoutStrategyKey(int index);

// 应用排版策略到 TextBlock 列表
// blocks: 输入输出参数, 会被原地修改 (排序、设置 end 字段)
// strategyKey: 策略 key, 默认 "multi_line"
void applyLayout(std::vector<TextBlock>& blocks, const std::string& strategyKey = "");

#endif // __TBPULAYOUT_H__
