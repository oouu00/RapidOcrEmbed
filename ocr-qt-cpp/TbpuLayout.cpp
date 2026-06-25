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

// ============================================================
// TBPU 排版算法 C++ 实现
// 混合了 Umi-OCR 的 GapTree 多栏版面分析算法和 k2pdfopt 的精细排序算法
// Umi-OCR 项目: https://github.com/hiroi-sora/Umi-OCR
// k2pdfopt 项目: http://willus.com/k2pdfopt/
// 两者均为开源软件, 符合开源合规要求
//
// 移植自 Umi-OCR Python 实现 (UmiOCR-data/py_src/ocr/tbpu/):
// 本算法混合了 Umi-OCR 的 GapTree 多栏版面分析算法和 k2pdfopt 的精细排序算法
// k2pdfopt 项目: http://willus.com/k2pdfopt/
// 两者均为开源软件, 符合开源合规要求
//   parser_tools/line_preprocessing.py
//   parser_tools/gap_tree.py
//   parser_tools/paragraph_parse.py
//   parser_single_line.py / parser_single_para.py
//   parser_single_none.py / parser_single_code.py
//   parser_multi_line.py / parser_multi_para.py / parser_multi_none.py
//   __init__.py (7 策略分发)
// 阈值常量与判定逻辑完全保持一致, 不做任何精简。
// 唯一非算法性适配: word_separator 中 unicodedata.category().startswith("P")
//   用 isPunct 表 (ASCII P 类全集 + 常见 Unicode 标点) 实现。
// ============================================================

#include "TbpuLayout.h"

// MSVC 默认不定义 M_PI，需要手动定义
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// 旋转角度阈值 (度), 对齐 line_preprocessing.angle_threshold = 3
const double TBPU_ANGLE_THRESHOLD = 3.0;
// 段落对齐/行间距对比阈值 (×行高), 对齐 paragraph_parse.TH = 1.2
const double TBPU_TH = 1.2;

// layoutStrategies() 由静态库 RapidOcrOnnxStatic.lib 提供, 这里不再重复定义

// ============================================================
// 内部工具: Unicode / 字符串
// ============================================================

// 从 UTF-8 字符串中解析出第一个码点 (等价 Python 的 text[0], Python3 字符串为 Unicode)
static uint32_t utf8FirstCodepoint(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    uint32_t cp = 0;
    unsigned char c = p[0];
    int extra = 0;
    if ((c & 0x80) == 0) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = c; extra = 0; } // 非法首字节, 退化为单字节
    for (int i = 1; i <= extra && (size_t)i < s.size(); ++i) {
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    return cp;
}

// 从 UTF-8 字符串中解析出最后一个码点 (等价 Python 的 text[-1])
static uint32_t utf8LastCodepoint(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = s.size();
    // 向前跳过所有 continuation 字节 (10xxxxxx)
    while (i > 1 && (p[i - 1] & 0xC0) == 0x80) {
        --i;
    }
    --i; // 指向序列首字节
    // 从首字节 i 开始, 重新解码一个完整序列
    uint32_t cp = 0;
    unsigned char c = p[i];
    int extra = 0;
    if ((c & 0x80) == 0) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = c; extra = 0; }
    for (int k = 1; k <= extra && i + k < s.size(); ++k) {
        cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    return cp;
}

// 判断码点是否属于中文/日文/韩文字符集 (对齐 paragraph_parse.is_cjk 的 8 个区间)
static bool isCjk(uint32_t cp) {
    // (start, end)
    static const std::pair<uint32_t, uint32_t> ranges[] = {
        {0x4E00, 0x9FFF}, // 中文
        {0x3040, 0x30FF}, // 日文
        {0x1100, 0x11FF}, // 韩文
        {0x3130, 0x318F}, // 韩文兼容字母
        {0xAC00, 0xD7AF}, // 韩文音节
        {0x3000, 0x303F}, // 中文符号和标点
        {0xFE30, 0xFE4F}, // 中文兼容形式标点
        {0xFF00, 0xFFEF}, // 半角和全角形式字符
    };
    for (const auto& pr : ranges) {
        if (cp >= pr.first && cp <= pr.second) return true;
    }
    return false;
}

// 判断码点是否为 Unicode "P" 类标点 (对齐 unicodedata.category(letter2).startswith("P"))
// C++ 标准库无 Unicode 类别表, 这里覆盖:
//   - ASCII 全部 P* 字符 (General Category=Punctuation 的 ASCII 子集)
//   - 常见非 ASCII 标点 (CJK 标点已由 isCjk 区间覆盖, 故这里补拉丁/全角等)
static bool isPunct(uint32_t cp) {
    // ASCII P* 字符全集 (UnicodeData 中 General Category 以 P 开头, codepoint < 0x80)
    // ! " # % & ' ( ) * , - . / : ; ? @ [ \ ] _ { }
    static const uint32_t asciiPunct[] = {
        0x21, 0x22, 0x23, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
        0x2C, 0x2D, 0x2E, 0x2F,
        0x3A, 0x3B, 0x3F, 0x40,
        0x5B, 0x5C, 0x5D, 0x5F, 0x7B, 0x7D
    };
    if (cp < 0x80) {
        for (uint32_t p : asciiPunct) {
            if (cp == p) return true;
        }
        return false;
    }
    // 常见非 ASCII 标点区间 (Pd/Pi/Pf/Ps/Pe/Pc/Po/Sm 子集)
    // 覆盖一般排版/OCR 常见标点; CJK 标点已由 isCjk 覆盖, 故不在此重复。
    if (cp == 0x00A1 || cp == 0x00A7 || cp == 0x00AB || cp == 0x00B6 ||
        cp == 0x00B7 || cp == 0x00BB || cp == 0x00BF) return true;
    if (cp >= 0x2010 && cp <= 0x2027) return true; // –—‖‘’“”†‡•…‰′″‴‵‶‷‸※‼‽‾‿
    if (cp >= 0x2030 && cp <= 0x205E) return true; // ‰′″‴‵‶‷‸‼‽ 等
    if (cp >= 0x2080 && cp <= 0x208E) return true; // 下标括号等
    if (cp == 0x2E2E || cp == 0x2E3A || cp == 0x2E3B) return true;
    if (cp >= 0x2768 && cp <= 0x2775) return true; // 装饰性括号
    if (cp >= 0x27C5 && cp <= 0x27C6) return true;
    if (cp >= 0x27E6 && cp <= 0x27EF) return true;
    if (cp >= 0x2983 && cp <= 0x2998) return true;
    if (cp >= 0x29D8 && cp <= 0x29DB) return true;
    if (cp >= 0x29FC && cp <= 0x29FD) return true;
    if (cp >= 0x2CF9 && cp <= 0x2CFC) return true;
    if (cp == 0x2CFE || cp == 0x2CFF) return true;
    if (cp == 0x2E00 || cp == 0x2E01 || cp == 0x2E06 || cp == 0x2E07 ||
        cp == 0x2E08 || cp == 0x2E0B) return true;
    if (cp >= 0x2E1C && cp <= 0x2E1D) return true;
    if (cp >= 0x2E20 && cp <= 0x2E29) return true;
    if (cp >= 0x2E9B && cp <= 0x2E9C) return true;
    if (cp >= 0xFE10 && cp <= 0xFE19) return true; // 竖排形式 (注: FE30-FE4F 已属 CJK)
    if (cp >= 0xFE41 && cp <= 0xFE44) return true;
    if (cp >= 0xFE47 && cp <= 0xFE48) return true;
    if (cp >= 0xFE56 && cp <= 0xFE5A) return true;
    if (cp >= 0xFE63 && cp <= 0xFE63) return true;
    if (cp >= 0xFE68 && cp <= 0xFE68) return true;
    if (cp >= 0xFE6A && cp <= 0xFE6B) return true;
    if (cp == 0xFF61) return true; // 半角片假名句号 (其余 FF 区已属 CJK)
    return false;
}

// 传入前句尾字符和后句首字符, 返回分隔符 (1:1 翻译 paragraph_parse.word_separator)
static std::string wordSeparator(uint32_t cp1, uint32_t cp2) {
    if (isCjk(cp1) && isCjk(cp2)) {
        return "";
    }
    // 特殊情况: 前文为连字符。
    if (cp1 == '-') {
        return "";
    }
    // 特殊情况: 后文为任意标点符号。
    if (isPunct(cp2)) {
        return "";
    }
    // 其它正常情况加空格
    return " ";
}
// 便捷重载: 直接传 UTF-8 文本 (取首/尾码点)
static std::string wordSeparatorText(const std::string& letter1Text, const std::string& letter2Text) {
    return wordSeparator(utf8LastCodepoint(letter1Text), utf8FirstCodepoint(letter2Text));
}

// ============================================================
// box (8 个 int: x0,y0,x1,y1,x2,y2,x3,y3 = TL,TR,BR,BL) 的 4 个点访问
// box[2*i + 0/1] 对应 py box[i][0/1]
// ============================================================
static inline double boxPointX(const OcrBlock& b, int i) { return (double)b.box[2 * i]; }
static inline double boxPointY(const OcrBlock& b, int i) { return (double)b.box[2 * i + 1]; }

// ============================================================
// 1. line_preprocessing (按行预处理)
// ============================================================

// 计算两点之间的距离 (对应 _distance)
static inline double ppDistance(double x1, double y1, double x2, double y2) {
    return std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

// 计算一个 box 的旋转角度弧度 (对应 _calculateAngle)
// box 为 4 个点: [TL, TR, BR, BL]
static double ppCalculateAngle(const OcrBlock& block) {
    double width = ppDistance(boxPointX(block, 0), boxPointY(block, 0),
                              boxPointX(block, 1), boxPointY(block, 1));
    double height = ppDistance(boxPointX(block, 1), boxPointY(block, 1),
                               boxPointX(block, 2), boxPointY(block, 2));
    double angle_rad;
    if (width < height) {
        angle_rad = std::atan2(boxPointY(block, 2) - boxPointY(block, 1),
                               boxPointX(block, 2) - boxPointX(block, 1));
    } else {
        angle_rad = std::atan2(boxPointY(block, 1) - boxPointY(block, 0),
                               boxPointX(block, 1) - boxPointX(block, 0));
    }
    double angle_threshold_rad = TBPU_ANGLE_THRESHOLD * M_PI / 180.0;
    if (angle_rad < -M_PI / 2.0 + angle_threshold_rad) {
        angle_rad += M_PI;
    } else if (angle_rad >= M_PI / 2.0 + angle_threshold_rad) {
        angle_rad -= M_PI;
    }
    return angle_rad;
}

// 估计一组文本块的旋转角度 (对应 _estimateRotation): 取中位数
static double ppEstimateRotation(const std::vector<OcrBlock*>& tbs) {
    std::vector<double> angle_rads;
    angle_rads.reserve(tbs.size());
    for (OcrBlock* tb : tbs) {
        angle_rads.push_back(ppCalculateAngle(*tb));
    }
    if (angle_rads.empty()) return 0.0;
    std::sort(angle_rads.begin(), angle_rads.end());
    size_t n = angle_rads.size();
    // Python statistics.median: 奇数取中间, 偶数取中间两数的平均
    if (n % 2 == 1) {
        return angle_rads[n / 2];
    } else {
        return (angle_rads[n / 2 - 1] + angle_rads[n / 2]) / 2.0;
    }
}

// 旋转后 bbox 为 (minx, miny, maxx, maxy)
struct Nbbox { double x0, y0, x1, y1; };

// 获取旋转后的标准 bbox (对应 _getBboxes)
static std::vector<Nbbox> ppGetBboxes(const std::vector<OcrBlock*>& tbs, double rotation_rad) {
    double angle_threshold_rad = TBPU_ANGLE_THRESHOLD * M_PI / 180.0;
    std::vector<Nbbox> bboxes;
    bboxes.reserve(tbs.size());
    if (std::fabs(rotation_rad) <= angle_threshold_rad) {
        // 角度低于阈值: 直接构造 bbox
        for (OcrBlock* tb : tbs) {
            double minx = boxPointX(*tb, 0), maxx = minx;
            double miny = boxPointY(*tb, 0), maxy = miny;
            for (int i = 1; i < 4; ++i) {
                double x = boxPointX(*tb, i), y = boxPointY(*tb, i);
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
            bboxes.push_back({minx, miny, maxx, maxy});
        }
    } else {
        // 否则进行旋转
        double rad = -rotation_rad;
        double cos_angle = std::cos(rad);
        double sin_angle = std::sin(rad);
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = std::numeric_limits<double>::infinity();
        for (OcrBlock* tb : tbs) {
            double rx[4], ry[4];
            for (int i = 0; i < 4; ++i) {
                double x = boxPointX(*tb, i), y = boxPointY(*tb, i);
                rx[i] = cos_angle * x - sin_angle * y;
                ry[i] = sin_angle * x + cos_angle * y;
            }
            double minx = rx[0], maxx = rx[0], miny = ry[0], maxy = ry[0];
            for (int i = 1; i < 4; ++i) {
                if (rx[i] < minx) minx = rx[i];
                if (rx[i] > maxx) maxx = rx[i];
                if (ry[i] < miny) miny = ry[i];
                if (ry[i] > maxy) maxy = ry[i];
            }
            Nbbox bb{minx, miny, maxx, maxy};
            bboxes.push_back(bb);
            if (minx < min_x) min_x = minx;
            if (miny < min_y) min_y = miny;
        }
        // 如果旋转后存在负坐标, 整体平移使最小 x/y 为 0
        if (min_x < 0 || min_y < 0) {
            for (Nbbox& b : bboxes) {
                b.x0 -= min_x; b.y0 -= min_y;
                b.x1 -= min_x; b.y1 -= min_y;
            }
        }
    }
    return bboxes;
}

// 预处理: 将 OcrBlock 列表过滤空 text, 计算归一化 bbox, 按 y 排序
// 返回 (有效块指针列表, 各块 nbbox, 与指针同序)。nbbox 与 ptrs 同序。
// 注意: 调用方需保证 tbs 容器在算法期间地址稳定。
struct Preprocessed {
    std::vector<OcrBlock*> tbs;       // 有效块指针 (已按 nbbox.y 排序)
    std::vector<Nbbox> nbboxes;       // 与 tbs 同序的归一化 bbox
};

static Preprocessed linePreprocessing(std::vector<OcrBlock>& blocks) {
    Preprocessed out;
    // 过滤空 text (对应 line_preprocessing 中 [i for i in textBlocks if i.get("text", False)])
    for (OcrBlock& b : blocks) {
        if (!b.text.empty()) {
            out.tbs.push_back(&b);
        }
    }
    if (out.tbs.empty()) return out;

    // 判断角度
    double rotation_rad = ppEstimateRotation(out.tbs);
    // 获取标准化 bbox
    std::vector<Nbbox> bboxes = ppGetBboxes(out.tbs, rotation_rad);
    out.nbboxes = std::move(bboxes);
    // 按 y (nbbox[1]) 排序: 构造索引, 稳定排序
    std::vector<size_t> idx(out.tbs.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return out.nbboxes[a].y0 < out.nbboxes[b].y0;
    });
    std::vector<OcrBlock*> sortedTbs;
    std::vector<Nbbox> sortedNb;
    sortedTbs.reserve(idx.size());
    sortedNb.reserve(idx.size());
    for (size_t i : idx) {
        sortedTbs.push_back(out.tbs[i]);
        sortedNb.push_back(out.nbboxes[i]);
    }
    out.tbs = std::move(sortedTbs);
    out.nbboxes = std::move(sortedNb);
    return out;
}

// ============================================================
// 2. GapTree 间隙树算法 (gap_tree.py)
// ============================================================

// 内部单元: ( (x0,y0,x2,y2), 原始块指针, 该块 nbbbox 索引 )
// 这里直接存 bbox 4 元组 + 指向 OcrBlock*
struct GtUnit {
    double bbox[4]; // x0, y0, x2, y2
    OcrBlock* tb;
};

class GapTree {
public:
    // get_bbox: 由块指针返回 (x0,y0,x2,y2)
    using GetBbox = std::function<Nbbox(OcrBlock*)>;

    GapTree(GetBbox get_bbox) : get_bbox_(std::move(get_bbox)) {}

    // 排序: 返回排序后的块指针序列 (平铺)
    std::vector<OcrBlock*> sort(const std::vector<OcrBlock*>& text_blocks) {
        double page_l, page_r;
        std::vector<GtUnit> units = getUnits(text_blocks, page_l, page_r);
        auto cr = getCutsRows(units, page_l, page_r);
        const std::vector<GtUnitCut>& cuts = cr.first;
        const std::vector<std::vector<GtUnit>>& rows = cr.second;
        getLayoutTree(cuts, rows); // 构建布局树 (结果落在 owned_nodes_)
        std::vector<LNode*> nodes = preorderTraversal();
        // 缓存中间变量
        current_rows_ = rows;
        current_cuts_ = cuts;
        current_nodes_.clear();
        for (LNode* n : nodes) current_nodes_.push_back(n);
        return getTextBlocks(nodes);
    }

    // 获取以区块为单位的文本块二层列表 (需要在 sort 后调用)
    std::vector<std::vector<OcrBlock*>> getNodesTextBlocks() const {
        std::vector<std::vector<OcrBlock*>> result;
        for (LNode* node : current_nodes_) {
            if (!node->units.empty()) {
                std::vector<OcrBlock*> tbs;
                for (const GtUnit& u : node->units) tbs.push_back(u.tb);
                result.push_back(std::move(tbs));
            }
        }
        return result;
    }

private:
    GetBbox get_bbox_;
    // 缓存
    std::vector<std::vector<GtUnit>> current_rows_;
    struct GtUnitCut { double l, r; int r_start, r_end; }; // (左x, 右x, 起始行, 结束行)
    std::vector<GtUnitCut> current_cuts_;

    // 布局树节点
    struct LNode {
        double x_left, x_right;
        int r_top, r_bottom;
        std::vector<GtUnit> units;
        std::vector<LNode*> children;
    };
    std::vector<LNode*> current_nodes_; // 指向 owned_nodes_ 中的节点
    std::vector<std::unique_ptr<LNode>> owned_nodes_;

    // ====== _get_units ======
    std::vector<GtUnit> getUnits(const std::vector<OcrBlock*>& text_blocks, double& page_l, double& page_r) {
        std::vector<GtUnit> units;
        page_l = std::numeric_limits<double>::infinity();
        page_r = -1;
        for (OcrBlock* tb : text_blocks) {
            Nbbox b = get_bbox_(tb);
            GtUnit u;
            u.bbox[0] = b.x0; u.bbox[1] = b.y0; u.bbox[2] = b.x1; u.bbox[3] = b.y1;
            u.tb = tb;
            units.push_back(u);
            if (b.x0 < page_l) page_l = b.x0;
            if (b.x1 > page_r) page_r = b.x1;
        }
        // 按顶部 (bbox[1]) 从上到下排序
        std::stable_sort(units.begin(), units.end(), [](const GtUnit& a, const GtUnit& b) {
            return a.bbox[1] < b.bbox[1];
        });
        return units;
    }

    // ====== update_gaps ======
    // gap: (l, r, start_row)
    struct Gap { double l, r; int start_row; };
    // 返回: (更新后的 gaps1, gaps1 中被彻底移除的项)
    static std::pair<std::vector<Gap>, std::vector<Gap>> updateGaps(const std::vector<Gap>& gaps1,
                                                                    const std::vector<Gap>& gaps2) {
        std::vector<char> flags1(gaps1.size(), 1); // 是否彻底移除
        std::vector<char> flags2(gaps2.size(), 1); // 是否新加入
        std::vector<Gap> new_gaps1;
        for (size_t i1 = 0; i1 < gaps1.size(); ++i1) {
            const Gap& g1 = gaps1[i1];
            double l1 = g1.l, r1 = g1.r;
            for (size_t i2 = 0; i2 < gaps2.size(); ++i2) {
                const Gap& g2 = gaps2[i2];
                double l2 = g2.l, r2 = g2.r;
                double inter_l = std::max(l1, l2);
                double inter_r = std::min(r1, r2);
                if (inter_l <= inter_r) {
                    new_gaps1.push_back({inter_l, inter_r, g1.start_row});
                    flags1[i1] = 0;
                    flags2[i2] = 0;
                }
            }
        }
        for (size_t i2 = 0; i2 < gaps2.size(); ++i2) {
            if (flags2[i2]) new_gaps1.push_back(gaps2[i2]);
        }
        std::vector<Gap> del_gaps1;
        for (size_t i1 = 0; i1 < gaps1.size(); ++i1) {
            if (flags1[i1]) del_gaps1.push_back(gaps1[i1]);
        }
        return {new_gaps1, del_gaps1};
    }

    // ====== _get_cuts_rows ======
    std::pair<std::vector<GtUnitCut>, std::vector<std::vector<GtUnit>>> getCutsRows(
        const std::vector<GtUnit>& units, double page_l, double page_r) {
        page_l -= 1; // 保证页面左右边缘不与文本块重叠
        page_r += 1;
        std::vector<std::vector<GtUnit>> rows;
        std::vector<GtUnitCut> completed_cuts;
        std::vector<Gap> gaps;
        int row_index = 0;
        size_t unit_index = 0;
        size_t l_units = units.size();
        while (unit_index < l_units) {
            // ===== 查找当前行 row =====
            const GtUnit& unit = units[unit_index];
            double u_bottom = unit.bbox[3];
            std::vector<GtUnit> row;
            row.push_back(unit);
            for (size_t i = unit_index + 1; i < units.size(); ++i) {
                const GtUnit& next_u = units[i];
                double next_top = next_u.bbox[1];
                if (next_top > u_bottom) break;
                row.push_back(next_u);
                unit_index = i;
            }
            // ===== 查找当前行的间隙 row_gaps =====
            std::sort(row.begin(), row.end(), [](const GtUnit& a, const GtUnit& b) {
                if (a.bbox[0] != b.bbox[0]) return a.bbox[0] < b.bbox[0];
                return a.bbox[2] < b.bbox[2];
            });
            std::vector<Gap> row_gaps;
            double search_start = page_l;
            for (const GtUnit& u : row) {
                double l = u.bbox[0], r = u.bbox[2];
                if (l > search_start) {
                    row_gaps.push_back({search_start, l, row_index});
                }
                if (r > search_start) {
                    search_start = r;
                }
            }
            row_gaps.push_back({search_start, page_r, row_index});
            // ===== 更新考察中的间隙组 =====
            auto ug = updateGaps(gaps, row_gaps);
            gaps = ug.first;
            const std::vector<Gap>& del_gaps = ug.second;
            int row_max = row_index - 1;
            for (const Gap& dg1 : del_gaps) {
                completed_cuts.push_back({dg1.l, dg1.r, dg1.start_row, row_max});
            }
            // ===== End =====
            rows.push_back(row);
            unit_index += 1;
            row_index += 1;
        }
        // 遍历结束, 收集 gaps 中剩余的间隙
        int row_max = (int)rows.size() - 1;
        for (const Gap& g : gaps) {
            completed_cuts.push_back({g.l, g.r, g.start_row, row_max});
        }
        std::sort(completed_cuts.begin(), completed_cuts.end(),
                  [](const GtUnitCut& a, const GtUnitCut& b) { return a.l < b.l; });
        return {completed_cuts, rows};
    }

    // ====== _get_layout_tree ======
    LNode* newLNode() {
        owned_nodes_.push_back(std::unique_ptr<LNode>(new LNode()));
        return owned_nodes_.back().get();
    }

    void getLayoutTree(const std::vector<GtUnitCut>& cuts,
                       const std::vector<std::vector<GtUnit>>& rows) {
        // 每行对应的间隙 (l, r) 列表
        std::vector<std::vector<std::pair<double, double>>> rows_gaps(rows.size());
        for (const GtUnitCut& cut : cuts) {
            for (int r_i = cut.r_start; r_i <= cut.r_end; ++r_i) {
                rows_gaps[r_i].push_back({cut.l, cut.r});
            }
        }

        // 根节点 (用 owned 持有以稳定地址; 它是 owned_nodes_[0])
        LNode* rootPtr = newLNode();
        rootPtr->x_left = cuts[0].l - 1;
        rootPtr->x_right = cuts.back().r + 1;
        rootPtr->r_top = -1;
        rootPtr->r_bottom = -1;
        rootPtr->units.clear();
        rootPtr->children.clear();

        std::vector<LNode*> completed_nodes;
        std::vector<LNode*> now_nodes;
        // root 作为第一个完成节点
        completed_nodes.push_back(rootPtr);

        // ===== complete(node) =====
        // 内部使用 completed_nodes, max_nodes 中选 x_right 最大者作父
        auto complete = [&](LNode* node) {
            double node_r = node->x_right - 2; // 当前节点右边界
            std::vector<LNode*> max_nodes;     // 符合父节点条件的, 最低的完成节点列表
            int max_r = -2;                    // 符合父节点条件的最低行数
            for (LNode* com_node : completed_nodes) {
                // 父节点的垂直投影必须包含当前右界
                if (node_r < com_node->x_left || node_r > com_node->x_right + 0.0001) continue;
                // 父节点底部必须在当前之上
                if (com_node->r_bottom >= node->r_top) continue;
                // 遇到更低的符合条件节点
                if (com_node->r_bottom > max_r) {
                    max_r = com_node->r_bottom;
                    max_nodes = {com_node};
                    continue;
                }
                // 遇到同样低的符合条件节点
                if (com_node->r_bottom == max_r) {
                    max_nodes.push_back(com_node);
                    continue;
                }
            }
            // 在最低列表中, 寻找最右的节点作为父节点
            LNode* max_node = nullptr;
            double best_xr = -std::numeric_limits<double>::infinity();
            for (LNode* n : max_nodes) {
                if (n->x_right > best_xr) { best_xr = n->x_right; max_node = n; }
            }
            max_node->children.push_back(node); // 加入父节点
            completed_nodes.push_back(node);    // 加入完成列表
        };

        // ===== 遍历每行, 更新节点树 =====
        for (size_t r_i_sz = 0; r_i_sz < rows.size(); ++r_i_sz) {
            int r_i = (int)r_i_sz;
            const std::vector<GtUnit>& row = rows[r_i_sz];
            const std::vector<std::pair<double, double>>& row_gaps = rows_gaps[r_i_sz];
            size_t u_i = 0, g_i = 0;

            // ===== 检查是否有正在考虑的节点可以结束 =====
            std::vector<LNode*> new_nodes;
            for (LNode* node : now_nodes) {
                bool l_flag = false, r_flag = false;
                bool completed_flag = false;
                double x_left = node->x_left, x_right = node->x_right;
                for (const std::pair<double, double>& gap : row_gaps) {
                    if (gap.second == x_left) l_flag = true;  // 节点左边缘被间隙右侧延续
                    if (gap.first == x_right) r_flag = true;  // 右边缘被间隙左侧延续
                    // 任意间隙在本节点下方, 打断本节点
                    if ((x_left < gap.first && gap.first < x_right) ||
                        (x_left < gap.second && gap.second < x_right)) {
                        completed_flag = true;
                        break;
                    }
                }
                if (!l_flag || !r_flag) completed_flag = true;
                if (completed_flag) {
                    complete(node);
                } else {
                    node->r_bottom = r_i;
                    new_nodes.push_back(node);
                }
            }
            now_nodes = new_nodes;

            // ===== 从左到右遍历, 将文本块加入对应列的节点 =====
            while (u_i < row.size()) {
                const GtUnit& unit = row[u_i];
                // 当前块 unit 位于间隙 g_i 与 g_i+1 之间的区间
                double x_l = row_gaps[g_i].second;  // 左间隙 g_i 的右边界
                double x_r = row_gaps[g_i + 1].first; // 右间隙 g_i+1 的左边界
                // 检查区间是否正确
                if (unit.bbox[0] + 0.0001 > x_r) { // 块比右间隙更右, 说明到了下一个区间
                    g_i += 1;
                    continue;
                }
                // ===== 检查当前块可否加入已有的节点 =====
                bool flag = false;
                for (LNode* node : now_nodes) {
                    if (node->x_left == x_l && node->x_right == x_r) {
                        node->units.push_back(unit);
                        flag = true;
                        break;
                    }
                }
                if (flag) {
                    u_i += 1;
                    continue;
                }
                // ===== 根据当前块创建新的节点, 加入待考虑节点 =====
                LNode* nn = newLNode();
                nn->x_left = x_l;
                nn->x_right = x_r;
                nn->r_top = r_i;
                nn->r_bottom = r_i;
                nn->units = {unit};
                nn->children.clear();
                now_nodes.push_back(nn);
                u_i += 1;
            }
        }
        // 将剩余节点也加入节点树
        for (LNode* node : now_nodes) {
            complete(node);
        }
        // 整理所有节点
        for (LNode* node : completed_nodes) {
            // 所有子节点按从左到右排序
            std::sort(node->children.begin(), node->children.end(),
                      [](LNode* a, LNode* b) { return a->x_left < b->x_left; });
            // 所有块单元按从上到下排序
            std::sort(node->units.begin(), node->units.end(),
                      [](const GtUnit& a, const GtUnit& b) { return a.bbox[1] < b.bbox[1]; });
        }
        // 结果落在 owned_nodes_ (节点树), 根节点为 owned_nodes_[0]
    }

    // ====== _preorder_traversal ======
    std::vector<LNode*> preorderTraversal() {
        std::vector<LNode*> result;
        // 约定: owned_nodes_[0] 即根节点
        if (owned_nodes_.empty()) return result;
        LNode* rootPtr = owned_nodes_[0].get();
        std::vector<LNode*> stack;
        stack.push_back(rootPtr);
        while (!stack.empty()) {
            LNode* node = stack.back();
            stack.pop_back();
            result.push_back(node);
            // 将子节点逆序压入栈中, 保证左子节点先处理
            for (size_t i = node->children.size(); i-- > 0;) {
                stack.push_back(node->children[i]);
            }
        }
        return result;
    }

    // ====== _get_text_blocks ======
    std::vector<OcrBlock*> getTextBlocks(const std::vector<LNode*>& nodes) const {
        std::vector<OcrBlock*> result;
        for (LNode* node : nodes) {
            for (const GtUnit& unit : node->units) {
                result.push_back(unit.tb);
            }
        }
        return result;
    }
};

// ============================================================
// 3. FineSort 精细排序算法 (移植自 text_block_reorder_v2.py)
// 用于区块内部的精细排序，解决困住问题、字体匹配等
// ============================================================

class FineSort {
public:
    // 排序参数
    struct Params {
        double col_misalign_max = 0.3;      // 列最大不对齐度 (0-1)
        double row_gap_min_ratio = 0.1;     // 最小行间距比例（相对于字体高度）
        double max_col_gap_ratio = 10.0;    // 最大列间距比例（相对于平均字符宽度）
        double font_size_epsilon_ratio = 0.15; // 字体大小差异阈值（比例）
        bool use_font_info = true;          // 是否使用字体信息
        bool left_to_right = true;          // 阅读方向
    };

    // 内部表示的文本块 (归一化坐标)
    struct NormBox {
        double x1, y1, x2, y2;   // 归一化坐标 (英寸)
        double font_size;        // 字体高度 (英寸)
        double baseline;         // 基线位置 (英寸)
        std::string text;        // 文本内容
        OcrBlock* tb;            // 原始块指针
        int next = -1;           // 下一个块索引
        int prev = -1;           // 上一个块索引
        bool ignore = false;     // 是否忽略
    };

    // 对区块内部的文本块进行精细排序
    // 输入: 区块内的 OcrBlock* 列表 + 归一化 bbox 映射
    // 输出: 排序后的 OcrBlock* 列表
    static std::vector<OcrBlock*> sortRegion(
        const std::vector<OcrBlock*>& region_blocks,
        const std::unordered_map<OcrBlock*, Nbbox>& nbMap,
        const Params& params = Params()) {

        if (region_blocks.size() < 2) {
            return region_blocks;
        }

        // 转换为 NormBox
        std::vector<NormBox> boxes = convertToNormBoxes(region_blocks, nbMap);

        // 计算平均字体高度和字符宽度
        double avg_font_height = 0.0, avg_char_width = 0.0;
        int font_count = 0, char_count = 0;
        for (const NormBox& box : boxes) {
            if (box.font_size > 0) {
                avg_font_height += box.font_size;
                font_count++;
            }
            double width = box.x2 - box.x1;
            int text_len = (int)box.text.size();
            if (width > 0 && text_len > 0) {
                avg_char_width += width / text_len;
                char_count++;
            }
        }
        if (font_count > 0) avg_font_height /= font_count;
        else avg_font_height = 0.15;
        if (char_count > 0) avg_char_width /= char_count;
        else avg_char_width = 0.1;

        // 执行排序算法
        std::vector<int> sorted_indices = sortRegionsFull(
            boxes, params, avg_font_height, avg_char_width);

        // 转换回 OcrBlock* 列表
        std::vector<OcrBlock*> result;
        result.reserve(sorted_indices.size());
        for (int idx : sorted_indices) {
            result.push_back(boxes[idx].tb);
        }
        return result;
    }

private:
    // 转换为 NormBox
    static std::vector<NormBox> convertToNormBoxes(
        const std::vector<OcrBlock*>& region_blocks,
        const std::unordered_map<OcrBlock*, Nbbox>& nbMap) {

        std::vector<NormBox> boxes;
        boxes.reserve(region_blocks.size());

        // DPI 假设为 72 (与 Python 版一致)
        const double DPI = 72.0;

        for (OcrBlock* tb : region_blocks) {
            Nbbox nb = nbMap.at(tb);
            NormBox box;
            box.x1 = nb.x0;
            box.y1 = nb.y0;
            box.x2 = nb.x1;
            box.y2 = nb.y1;
            box.text = tb->text;
            box.tb = tb;

            // 估算字体信息
            double width = nb.x1 - nb.x0;
            double height = nb.y1 - nb.y0;
            int char_count = (int)tb->text.size();

            // 字体高度 = 文本框高度
            box.font_size = height;

            // 基线位置 = 文本框底部往上约10%高度
            box.baseline = nb.y1 - height * 0.1;

            boxes.push_back(box);
        }
        return boxes;
    }

    // 计算两个区间的对齐度
    // 返回值: 1.0 = 完美对齐, 0 = 部分重叠, 负数 = 无重叠
    static double alignment(double x0, double x1, double x2, double x3) {
        double w0 = x1 - x0;
        double w1 = x3 - x2;
        double wmax = std::max(w0, w1);
        if (wmax < 0.01) wmax = 0.01;

        if (x2 > x1) return (x1 - x2) / wmax;
        if (x3 < x0) return (x3 - x0) / wmax;
        if (x2 < x0) {
            if (x3 > x1) return (x1 - x0) / wmax;
            else return (x3 - x0) / wmax;
        } else {
            if (x3 > x1) return (x1 - x2) / wmax;
            else return (x3 - x2) / wmax;
        }
    }

    // 计算两个文本块之间的距离关系
    static std::tuple<double, double, double> determineCloseness(
        const NormBox& box0, const NormBox& box1) {

        double col_align = alignment(box0.x1, box0.x2, box1.x1, box1.x2);
        double row_align = alignment(box0.y1, box0.y2, box1.y1, box1.y2);

        double gap;
        if (col_align > 0) {
            gap = box1.y1 - box0.y2;  // 垂直间距
        } else {
            gap = box1.x1 - box0.x2;  // 水平间距
        }
        return std::make_tuple(gap, col_align, row_align);
    }

    // 比较两个文本块的位置
    static double positionCompare(const NormBox& box1, const NormBox& box2) {
        bool vertical_overlap = (box2.y2 > box1.y1 && box2.y1 < box1.y2);
        bool horizontal_overlap = (box2.x2 > box1.x1 && box2.x1 < box1.x2);

        if (vertical_overlap) return box1.x1 - box2.x1;
        if (horizontal_overlap) return box1.y1 - box2.y1;
        return (box1.x1 + box1.y1) - (box2.x1 + box2.y1);
    }

    // 找到距离最近的下一个文本块
    static std::tuple<int, double, double> findClosest(
        const NormBox& last_box, const std::vector<NormBox>& boxes,
        bool horizontal_overlap, double epsilon = 0.1) {

        int best_idx = -1;
        double best_align = -1.0;
        double best_gap = 99.0;

        for (size_t i = 0; i < boxes.size(); ++i) {
            const NormBox& box = boxes[i];
            if (box.ignore || box.prev >= 0) continue;

            double gap, col_align, row_align;
            std::tie(gap, col_align, row_align) = determineCloseness(last_box, box);

            if (gap < 0) continue;

            double align;
            if (horizontal_overlap) {
                if (col_align <= 0) continue;
                align = col_align;
            } else {
                if (row_align <= 0) continue;
                align = row_align;
            }

            if (best_idx < 0 || gap < best_gap - epsilon ||
                (std::fabs(gap - best_gap) <= epsilon && boxes[best_idx].x1 > box.x1)) {
                best_idx = (int)i;
                best_gap = gap;
                best_align = align;
            }
        }
        return std::make_tuple(best_idx, best_align, best_gap);
    }

    // 检查字体大小和行间距匹配
    static bool checkSpacingMatch(const NormBox& box0, const NormBox& box1,
                                   const Params& params) {
        if (!params.use_font_info) return false;
        if (!(box0.font_size > 0 && box1.font_size > 0)) return false;

        double avg_font_size = (box0.font_size + box1.font_size) / 2.0;
        double font_diff = std::fabs(box0.font_size - box1.font_size);

        if (font_diff > avg_font_size * params.font_size_epsilon_ratio) return false;

        if (box0.baseline > 0 && box1.baseline > 0) {
            double gap = std::fabs(box1.baseline - box0.baseline);
            double expected_gap = box0.font_size * 1.2;
            double gap_diff = std::fabs(gap - expected_gap);
            if (gap_diff > avg_font_size * params.font_size_epsilon_ratio) return false;
        }
        return true;
    }

    // 困住检测
    static bool trappedBox(int ibox, std::vector<NormBox>& boxes, int n, double comax) {
        if (comax > 0.2) comax = 0.2;

        std::vector<int> trapped(n, 0);

        // 找到序列的第一个元素
        int first = ibox;
        int count = 0;
        while (count < n) {
            int prev_idx = boxes[first].prev;
            if (prev_idx >= 0 && prev_idx < n) {
                if (boxes[prev_idx].next == first) first = prev_idx;
                else break;
            } else break;
            count++;
        }

        // 标记已连接序列中的所有块
        int idx = first;
        while (idx >= 0 && idx < n) {
            trapped[idx] = 255;
            idx = boxes[idx].next;
        }

        // 遍历序列中的每个块，检查周围是否有未连接的块
        int ibox_iter = first;
        while (ibox_iter >= 0 && ibox_iter < n) {
            const NormBox& box = boxes[ibox_iter];
            double width = box.x2 - box.x1;
            double height = box.y2 - box.y1;

            double ibx1 = box.x1 + width * comax;
            double ibx2 = box.x2 - width * comax;
            double iby1 = box.y1 + height * comax;
            double iby2 = box.y2 - height * comax;

            for (int i = 0; i < n; ++i) {
                if (trapped[i] == 255) continue;
                if (boxes[i].prev >= 0) continue;

                // 检查上方
                if (boxes[i].y2 <= box.y1) {
                    if (boxes[i].x2 >= ibx1 && boxes[i].x1 <= ibx2) trapped[i] |= 1;
                }
                // 检查下方
                if (boxes[i].y1 >= box.y2) {
                    if (boxes[i].x2 >= ibx1 && boxes[i].x1 <= ibx2) trapped[i] |= 8;
                }
                // 检查左侧
                if (boxes[i].x2 <= box.x1) {
                    if (boxes[i].y1 <= iby2 && boxes[i].y2 >= iby1) trapped[i] |= 2;
                }
                // 检查右侧
                if (boxes[i].x1 >= box.x2) {
                    if (boxes[i].y1 <= iby2 && boxes[i].y2 >= iby1) trapped[i] |= 4;
                }
            }
            ibox_iter = boxes[ibox_iter].next;
        }

        // 检查是否有被困住的块
        for (int i = 0; i < n; ++i) {
            if (trapped[i] != 255) {
                if ((trapped[i] & 3) == 3 || (trapped[i] & 5) == 5 || (trapped[i] & 10) == 10) {
                    return true;
                }
            }
        }
        return false;
    }

    // 完整的排序算法 (7轮尝试连接)
    static std::vector<int> sortRegionsFull(
        std::vector<NormBox>& boxes, const Params& params,
        double avg_font_height, double avg_char_width) {

        int n = (int)boxes.size();
        if (n < 2) {
            std::vector<int> result;
            for (int i = 0; i < n; ++i) result.push_back(i);
            return result;
        }

        // 如果是从右到左阅读，翻转x坐标
        if (!params.left_to_right) {
            double xmin = std::numeric_limits<double>::infinity();
            double xmax = -std::numeric_limits<double>::infinity();
            for (NormBox& box : boxes) {
                xmin = std::min(xmin, box.x1);
                xmax = std::max(xmax, box.x2);
            }
            for (NormBox& box : boxes) {
                double new_x1 = xmin + (xmax - box.x2);
                double new_x2 = xmin + (xmax - box.x1);
                box.x1 = new_x1;
                box.x2 = new_x2;
            }
        }

        // 找到最左上角的文本块作为起始点
        std::vector<int> sorted_indices;
        sorted_indices.push_back(0);
        for (int i = 1; i < n; ++i) {
            if (boxes[i].ignore) continue;
            if (positionCompare(boxes[i], boxes[sorted_indices[0]]) < 0) {
                sorted_indices[0] = i;
            }
        }
        boxes[sorted_indices[0]].prev = 9999;

        // 7轮尝试连接
        const bool check_above_below[] = {true, true, false, true, false, true, false};
        const double gap_thresh_array[] = {-1.0, 0.0, 0.0, 0.25, 0.25, 0.5, 0.5};
        const double align_thresh_array[] = {1.0, 1.0, 0.98, 0.75, 0.75, 0.5, 0.5};

        for (int try_round = 0; try_round < 7; ++try_round) {
            double gap_thresh = gap_thresh_array[try_round];
            double align_thresh = align_thresh_array[try_round];

            if (check_above_below[try_round]) {
                align_thresh *= (1.0 - params.col_misalign_max);
            }

            // 使用比例计算阈值
            if (std::fabs(gap_thresh) < 0.0001) {
                if (check_above_below[try_round]) {
                    gap_thresh = avg_font_height * params.row_gap_min_ratio;
                } else {
                    gap_thresh = avg_char_width * params.max_col_gap_ratio;
                }
            }

            double epsilon = 0.1;

            for (int i = 0; i < n; ++i) {
                if (boxes[i].ignore || boxes[i].next >= 0) continue;

                boxes[i].next = i;  // 临时标记

                int best_below = -1, best_right = -1;
                double h_align = 0, v_gap = 0, v_align = 0, h_gap = 0;
                bool spacing_match = false;

                if (check_above_below[try_round]) {
                    std::tie(best_below, h_align, v_gap) = findClosest(boxes[i], boxes, true, epsilon);
                    if (best_below >= 0 && boxes[best_below].ignore) best_below = -1;
                    if (best_below >= 0) {
                        spacing_match = checkSpacingMatch(boxes[i], boxes[best_below], params);
                    }
                } else {
                    std::tie(best_right, v_align, h_gap) = findClosest(boxes[i], boxes, false, epsilon);
                    if (best_right >= 0 && boxes[best_right].ignore) best_right = -1;
                }

                boxes[i].next = -1;
                int best = -1;

                // 判断是否满足相邻条件
                if (best_below >= 0) {
                    if ((gap_thresh < 0 && h_align >= align_thresh && spacing_match) ||
                        (h_align > 0 && h_align >= align_thresh && v_gap <= gap_thresh) ||
                        (v_gap <= gap_thresh && h_align >= align_thresh &&
                         (best_right < 0 || v_gap < h_gap + epsilon ||
                          boxes[best_below].y1 < boxes[best_right].y2))) {
                        best = best_below;
                    }
                } else {
                    if (best_right >= 0 && h_gap <= gap_thresh && v_align >= align_thresh &&
                        (best_below < 0 || h_gap < v_gap + epsilon)) {
                        best = best_right;
                    }
                }

                if (best >= 0) {
                    boxes[i].next = best;
                    boxes[best].prev = i;
                    // 检查是否困住
                    if (trappedBox(i, boxes, n, params.col_misalign_max)) {
                        boxes[i].next = -1;
                        boxes[best].prev = -1;
                    }
                }
            }
        }

        // 按连接顺序排列剩余的文本块
        for (int i = 1; i < n; ++i) {
            if (boxes[sorted_indices[i-1]].next >= 0) {
                sorted_indices.push_back(boxes[sorted_indices[i-1]].next);
                continue;
            }

            // 找下一个未连接的文本块
            int best = -1;
            for (int j = 0; j < n; ++j) {
                if (boxes[j].ignore || j == sorted_indices[i-1]) continue;
                if (boxes[j].prev >= 0) continue;
                if (best < 0 || positionCompare(boxes[j], boxes[best]) < 0) best = j;
            }

            if (best >= 0) {
                sorted_indices.push_back(best);
                boxes[sorted_indices[i-1]].next = best;
                boxes[best].prev = sorted_indices[i-1];
            }
        }

        return sorted_indices;
    }
};

// ============================================================
// 4. ParagraphParse 段落分析器 (paragraph_parse.py)
// ============================================================
// 因 OcrBlock 不可加字段, 用 get_info / set_end 回调抽象;
// 单元内部表示: (bbox[4], first/last 码点, 用户句柄)

class ParagraphParse {
public:
    // GetInfo: 传入用户句柄, 返回 (bbox, 文本首码点, 文本尾码点)
    using GetInfo = std::function<std::tuple<Nbbox, uint32_t, uint32_t>(void* handle)>;
    // SetEnd: 传入用户句柄和分隔符
    using SetEnd = std::function<void(void* handle, const std::string& end)>;

    ParagraphParse(GetInfo get_info, SetEnd set_end)
        : get_info_(std::move(get_info)), set_end_(std::move(set_end)) {}

    // 对文本块句柄列表进行结尾分隔符预测 (原样, 仅原地写 end)
    void run(const std::vector<void*>& text_blocks) {
        std::cerr << "[layout] ParagraphParse.run start: text_blocks.size=" << text_blocks.size() << std::endl;
        
        try {
            std::cerr << "[layout] ParagraphParse.run: calling getUnits..." << std::endl;
            std::vector<PpUnit> units = getUnits(text_blocks);
            std::cerr << "[layout] ParagraphParse.run: getUnits returned, units.size=" << units.size() << std::endl;
            
            std::cerr << "[layout] ParagraphParse.run: calling parse..." << std::endl;
            parse(units);
            std::cerr << "[layout] ParagraphParse.run: parse done" << std::endl;
            
            std::cerr << "[layout] ParagraphParse.run SUCCESS" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[layout] ParagraphParse.run EXCEPTION: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[layout] ParagraphParse.run UNKNOWN EXCEPTION" << std::endl;
            throw;
        }
    }

private:
    GetInfo get_info_;
    SetEnd set_end_;
    struct PpUnit {
        double bbox[4];     // x0,y0,x1,y1
        uint32_t cFirst, cLast; // ("开头","结尾")
        void* handle;       // 原始
    };
    double TH = TBPU_TH;

    // ===== _get_units =====
    std::vector<PpUnit> getUnits(const std::vector<void*>& text_blocks) {
        std::cerr << "[layout] ParagraphParse.getUnits start: text_blocks.size=" << text_blocks.size() << std::endl;
        
        std::vector<PpUnit> units;
        units.reserve(text_blocks.size());  // 预分配空间，避免动态扩容
        
        for (size_t i = 0; i < text_blocks.size(); ++i) {
            void* tb = text_blocks[i];
            std::cerr << "[layout] getUnits[" << i << "]: processing handle=" << tb << std::endl;
            
            try {
                auto info = get_info_(tb);
                std::cerr << "[layout] getUnits[" << i << "]: get_info returned" << std::endl;
                
                Nbbox b = std::get<0>(info);
                std::cerr << "[layout] getUnits[" << i << "]: bbox: x0=" << b.x0 << ", y0=" << b.y0 
                          << ", x1=" << b.x1 << ", y1=" << b.y1 << std::endl;
                
                PpUnit u;
                u.bbox[0] = b.x0; u.bbox[1] = b.y0; u.bbox[2] = b.x1; u.bbox[3] = b.y1;
                u.cFirst = std::get<1>(info);
                u.cLast = std::get<2>(info);
                u.handle = tb;
                
                std::cerr << "[layout] getUnits[" << i << "]: cFirst=" << u.cFirst 
                          << ", cLast=" << u.cLast << std::endl;
                
                units.push_back(u);
                std::cerr << "[layout] getUnits[" << i << "]: added to units, current size=" << units.size() << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "[layout] getUnits[" << i << "] EXCEPTION: " << e.what() << std::endl;
                throw;
            } catch (...) {
                std::cerr << "[layout] getUnits[" << i << "] UNKNOWN EXCEPTION" << std::endl;
                throw;
            }
        }
        
        std::cerr << "[layout] ParagraphParse.getUnits done: units.size=" << units.size() << std::endl;
        return units;
    }

    // ===== _parse =====
    void parse(std::vector<PpUnit>& units) {
        std::cerr << "[layout] ParagraphParse.parse start: units.size=" << units.size() << std::endl;
        
        if (units.empty()) {
            std::cerr << "[layout] ParagraphParse.parse: units empty, return" << std::endl;
            return;
        }
        
        std::cerr << "[layout] ParagraphParse.parse: sorting units..." << std::endl;
        // 确保从上到下有序
        std::stable_sort(units.begin(), units.end(), [](const PpUnit& a, const PpUnit& b) {
            return a.bbox[1] < b.bbox[1];
        });
        std::cerr << "[layout] ParagraphParse.parse: sorted" << std::endl;

        // 使用指针减少堆栈占用
        std::cerr << "[layout] ParagraphParse.parse: initializing variables..." << std::endl;
        
        double para_l = units[0].bbox[0];
        double para_top = units[0].bbox[1];
        double para_r = units[0].bbox[2];
        double para_bottom = units[0].bbox[3];
        double para_line_h = para_bottom - para_top; // 当前段行高
        bool para_line_s_valid = false;
        double para_line_s = 0.0;                    // 当前段行间距
        
        // 预分配 vector 空间，减少动态扩容
        std::vector<PpUnit*> now_para;
        now_para.reserve(units.size());  // 预分配最大可能大小
        now_para.push_back(&units[0]);   // 当前段的块
        
        std::vector<std::vector<PpUnit*>> paras;
        paras.reserve(units.size());     // 预分配最大可能段数
        
        std::vector<bool> paras_ls_valid;
        paras_ls_valid.reserve(units.size());
        
        std::vector<double> paras_line_space;
        paras_line_space.reserve(units.size());
        
        std::cerr << "[layout] ParagraphParse.parse: variables initialized" << std::endl;

        // 取 左右相等为一个自然段的主体
        std::cerr << "[layout] ParagraphParse.parse: processing units..." << std::endl;
        for (size_t i = 1; i < units.size(); ++i) {
            std::cerr << "[layout] ParagraphParse.parse: processing unit[" << i << "]" << std::endl;
            
            double l = units[i].bbox[0];
            double top = units[i].bbox[1];
            double r = units[i].bbox[2];
            double bottom = units[i].bbox[3];
            double h = bottom - top;
            double ls = top - para_bottom; // 行间距
            
            std::cerr << "[layout] ParagraphParse.parse: unit[" << i << "] bbox: l=" << l 
                      << ", top=" << top << ", r=" << r << ", bottom=" << bottom << std::endl;
            
            // 检测是否同一段
            bool same = (std::fabs(para_l - l) <= para_line_h * TH &&
                         std::fabs(para_r - r) <= para_line_h * TH &&
                         (!para_line_s_valid || ls < para_line_s + para_line_h * 0.5));
            
            std::cerr << "[layout] ParagraphParse.parse: unit[" << i << "] same=" << same << std::endl;
            
            if (same) {
                // 更新数据
                para_l = (para_l + l) / 2.0;
                para_r = (para_r + r) / 2.0;
                para_line_h = (para_line_h + h) / 2.0;
                if (!para_line_s_valid) { para_line_s = ls; para_line_s_valid = true; }
                else { para_line_s = (para_line_s + ls) / 2.0; }
                // 添加到当前段
                now_para.push_back(&units[i]);
                std::cerr << "[layout] ParagraphParse.parse: unit[" << i << "] added to current para" << std::endl;
            } else {
                // 非同一段, 归档上一段, 创建新一段
                std::cerr << "[layout] ParagraphParse.parse: unit[" << i << "] starting new para" << std::endl;
                
                paras.push_back(now_para);
                paras_ls_valid.push_back(para_line_s_valid);
                paras_line_space.push_back(para_line_s);
                
                std::cerr << "[layout] ParagraphParse.parse: archived para, paras.size=" << paras.size() << std::endl;
                
                now_para.clear();
                now_para.push_back(&units[i]);
                para_l = l;
                para_r = r;
                para_line_h = bottom - top;
                para_line_s_valid = false;
                para_line_s = 0.0;
                
                std::cerr << "[layout] ParagraphParse.parse: new para initialized" << std::endl;
            }
            para_bottom = bottom;
        }
        
        // 归档最后一段
        std::cerr << "[layout] ParagraphParse.parse: archiving last para" << std::endl;
        paras.push_back(now_para);
        paras_ls_valid.push_back(para_line_s_valid);
        paras_line_space.push_back(para_line_s);
        std::cerr << "[layout] ParagraphParse.parse: last para archived, paras.size=" << paras.size() << std::endl;

        // 合并只有1行的段, 添加到上/下段作为首/尾句
        std::cerr << "[layout] ParagraphParse.parse: merging single-line paras..." << std::endl;
        for (int i1 = (int)paras.size() - 1; i1 >= 0; --i1) {
            std::cerr << "[layout] ParagraphParse.parse: checking para[" << i1 << "]" << std::endl;
            
            std::vector<PpUnit*>& para = paras[i1];
            if (para.size() != 1) {
                std::cerr << "[layout] ParagraphParse.parse: para[" << i1 << "] size=" << para.size() << ", skip" << std::endl;
                continue;
            }
            
            std::cerr << "[layout] ParagraphParse.parse: para[" << i1 << "] is single-line, checking merge..." << std::endl;
            
            double l = para[0]->bbox[0];
            double top = para[0]->bbox[1];
            double r = para[0]->bbox[2];
            double bottom = para[0]->bbox[3];
            bool up_flag = false, down_flag = false;
            
            // 上段末尾条件: 左对齐, 右不超, 行间距够小
            if (i1 > 0) {
                std::cerr << "[layout] ParagraphParse.parse: checking up para[" << i1 - 1 << "]" << std::endl;
                
                PpUnit* upUnit = paras[i1 - 1].back();
                double up_l = upUnit->bbox[0], up_top = upUnit->bbox[1];
                double up_r = upUnit->bbox[2], up_bottom = upUnit->bbox[3];
                double up_dist = std::fabs(up_l - l);
                double up_h = up_bottom - up_top;
                up_flag = (up_dist <= up_h * TH) && (r <= up_r + up_h * TH);
                
                std::cerr << "[layout] ParagraphParse.parse: up_flag=" << up_flag << " (dist=" << up_dist << ", h=" << up_h << ")" << std::endl;
                
                // 检查行间距
                if (paras_ls_valid[i1 - 1] &&
                    top - up_bottom > paras_line_space[i1 - 1] + up_h * 0.5) {
                    up_flag = false;
                    std::cerr << "[layout] ParagraphParse.parse: up_flag=false (line spacing too large)" << std::endl;
                }
            }
            
            // 下段开头条件: 右对齐/单行超出, 左缩进
            if (i1 < (int)paras.size() - 1) {
                std::cerr << "[layout] ParagraphParse.parse: checking down para[" << i1 + 1 << "]" << std::endl;
                
                PpUnit* downUnit = paras[i1 + 1].front();
                double down_l = downUnit->bbox[0], down_top = downUnit->bbox[1];
                double down_r = downUnit->bbox[2], down_bottom = downUnit->bbox[3];
                double down_h = down_bottom - down_top;
                
                std::cerr << "[layout] ParagraphParse.parse: down_l=" << down_l << ", l=" << l << ", down_h=" << down_h << std::endl;
                
                // 左对齐或缩进
                if (down_l - down_h * TH <= l && l <= down_l + down_h * (1 + TH)) {
                    if (paras[i1 + 1].size() > 1) { // 多行, 右对齐
                        down_flag = std::fabs(down_r - r) <= down_h * TH;
                        std::cerr << "[layout] ParagraphParse.parse: down_flag=" << down_flag << " (multi-line, right align)" << std::endl;
                    } else { // 单行, 右可超出
                        down_flag = down_r - down_h * TH < r;
                        std::cerr << "[layout] ParagraphParse.parse: down_flag=" << down_flag << " (single-line, right can exceed)" << std::endl;
                    }
                } else {
                    std::cerr << "[layout] ParagraphParse.parse: down_flag=false (left indent condition not met)" << std::endl;
                }
                
                // 检查行间距
                if (paras_ls_valid[i1 + 1] &&
                    down_top - bottom > paras_line_space[i1 + 1] + down_h * 0.5) {
                    down_flag = false;
                    std::cerr << "[layout] ParagraphParse.parse: down_flag=false (line spacing too large)" << std::endl;
                }
            }
            
            // 选择添加到上还是下段
            std::cerr << "[layout] ParagraphParse.parse: up_flag=" << up_flag << ", down_flag=" << down_flag << std::endl;
            
            if (up_flag && down_flag) {
                // 两段都符合, 则选择垂直距离更近的
                double upDist = top - (paras[i1 - 1].back()->bbox[3]);
                double downDist = (paras[i1 + 1].front()->bbox[1]) - bottom;
                std::cerr << "[layout] ParagraphParse.parse: both flags true, upDist=" << upDist << ", downDist=" << downDist << std::endl;
                
                if (upDist < downDist) {
                    std::cerr << "[layout] ParagraphParse.parse: merging to up para" << std::endl;
                    paras[i1 - 1].push_back(para[0]);
                } else {
                    std::cerr << "[layout] ParagraphParse.parse: merging to down para" << std::endl;
                    paras[i1 + 1].insert(paras[i1 + 1].begin(), para[0]);
                }
            } else if (up_flag) {
                std::cerr << "[layout] ParagraphParse.parse: merging to up para only" << std::endl;
                paras[i1 - 1].push_back(para[0]);
            } else if (down_flag) {
                std::cerr << "[layout] ParagraphParse.parse: merging to down para only" << std::endl;
                paras[i1 + 1].insert(paras[i1 + 1].begin(), para[0]);
            }
            
            if (up_flag || down_flag) {
                std::cerr << "[layout] ParagraphParse.parse: erasing merged para[" << i1 << "]" << std::endl;
                paras.erase(paras.begin() + i1);
                paras_ls_valid.erase(paras_ls_valid.begin() + i1);
                paras_line_space.erase(paras_line_space.begin() + i1);
                std::cerr << "[layout] ParagraphParse.parse: para erased, paras.size=" << paras.size() << std::endl;
            }
        }
        
        std::cerr << "[layout] ParagraphParse.parse: merging done, paras.size=" << paras.size() << std::endl;

        // 刷新所有段, 添加 end
        std::cerr << "[layout] ParagraphParse.parse: refreshing paras, setting end..." << std::endl;
        int paraIndex = 0;
        for (std::vector<PpUnit*>& para : paras) {
            std::cerr << "[layout] ParagraphParse.parse: refreshing para[" << paraIndex << "], size=" << para.size() << std::endl;
            
            for (size_t i1 = 0; i1 + 1 < para.size(); ++i1) {
                std::cerr << "[layout] ParagraphParse.parse: para[" << paraIndex << "] line[" << i1 << "] setting end" << std::endl;
                
                uint32_t letter1 = para[i1]->cLast;      // 行1结尾字母
                uint32_t letter2 = para[i1 + 1]->cFirst; // 行2开头字母
                
                std::cerr << "[layout] ParagraphParse.parse: letter1=" << letter1 << ", letter2=" << letter2 << std::endl;
                
                std::string sep = wordSeparator(letter1, letter2);
                
                std::cerr << "[layout] ParagraphParse.parse: separator='" << sep << "'" << std::endl;
                
                try {
                    set_end_(para[i1]->handle, sep);
                    std::cerr << "[layout] ParagraphParse.parse: end set successfully" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[layout] ParagraphParse.parse: EXCEPTION in set_end_: " << e.what() << std::endl;
                    throw;
                } catch (...) {
                    std::cerr << "[layout] ParagraphParse.parse: UNKNOWN EXCEPTION in set_end_" << std::endl;
                    throw;
                }
            }
            
            std::cerr << "[layout] ParagraphParse.parse: para[" << paraIndex << "] setting last line end to newline" << std::endl;
            try {
                set_end_(para.back()->handle, "\n");
                std::cerr << "[layout] ParagraphParse.parse: last line end set successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[layout] ParagraphParse.parse: EXCEPTION in set_end_ (last): " << e.what() << std::endl;
                throw;
            } catch (...) {
                std::cerr << "[layout] ParagraphParse.parse: UNKNOWN EXCEPTION in set_end_ (last)" << std::endl;
                throw;
            }
            
            paraIndex++;
        }
        
        std::cerr << "[layout] ParagraphParse.parse: all paras refreshed, done" << std::endl;
    }
};

// ============================================================
// 4. SingleLine (单栏-单行) get_lines
// ============================================================
// 行内单元: 指向 OcrBlock* + 其 nbbox (来自预处理)
struct SlUnit {
    OcrBlock* tb;
    Nbbox nb;
};

// 从预处理后的块列表中找出所有行 (对应 SingleLine.get_lines)
// 返回: 每一行 (SlUnit 列表), 行内已设置每块 end, 行按行首 y 排序
static std::vector<std::vector<SlUnit>> singleLineGetLines(std::vector<SlUnit> tbs) {
    // 按 x (nbbox[0]) 排序
    std::stable_sort(tbs.begin(), tbs.end(), [](const SlUnit& a, const SlUnit& b) {
        return a.nb.x0 < b.nb.x0;
    });
    std::vector<std::vector<SlUnit>> lines;
    std::vector<char> consumed(tbs.size(), 0);
    for (size_t i1 = 0; i1 < tbs.size(); ++i1) {
        if (consumed[i1]) continue;
        // 最左的一个块
        double l1 = tbs[i1].nb.x0, top1 = tbs[i1].nb.y0;
        double r1 = tbs[i1].nb.x1, bottom1 = tbs[i1].nb.y1;
        double h1 = bottom1 - top1;
        std::vector<SlUnit> now_line = {tbs[i1]};
        // 考察右侧哪些块符合条件
        for (size_t i2 = i1 + 1; i2 < tbs.size(); ++i2) {
            if (consumed[i2]) continue;
            double l2 = tbs[i2].nb.x0, top2 = tbs[i2].nb.y0;
            double r2 = tbs[i2].nb.x1, bottom2 = tbs[i2].nb.y1;
            double h2 = bottom2 - top2;
            // 行2左侧太前
            if (l2 < r1 - h1) continue;
            // 垂直距离太远
            if (top2 < top1 - h1 * 0.5 || bottom2 > bottom1 + h1 * 0.5) continue;
            // 行高差距过大
            if (std::fabs(h1 - h2) > std::min(h1, h2) * 0.5) continue;
            // 符合条件
            now_line.push_back(tbs[i2]);
            consumed[i2] = 1;
            // 更新搜索条件
            r1 = r2;
        }
        // 处理完一行
        for (size_t i2 = 0; i2 + 1 < now_line.size(); ++i2) {
            // 检查同一行内相邻文本块的水平间隙
            double l1b = now_line[i2].nb.x0, t1 = now_line[i2].nb.y0;
            double r1b = now_line[i2].nb.x1, b1 = now_line[i2].nb.y1;
            double l2 = now_line[i2 + 1].nb.x0, t2 = now_line[i2 + 1].nb.y0;
            // r2, b2 未使用
            (void)t2;
            double b2 = now_line[i2 + 1].nb.y1;
            double h = (b1 + b2 - t1 - l2) * 0.5;
            if (l2 - r1b > h * 1.5) { // 间隙太大, 强制设置空格
                now_line[i2].tb->end = " ";
                continue;
            }
            uint32_t letter1 = utf8LastCodepoint(now_line[i2].tb->text);
            uint32_t letter2 = utf8FirstCodepoint(now_line[i2 + 1].tb->text);
            now_line[i2].tb->end = wordSeparator(letter1, letter2);
        }
        now_line.back().tb->end = "\n";
        lines.push_back(std::move(now_line));
        consumed[i1] = 1;
    }
    // 所有行按 y (行首 nbbox[1]) 排序
    std::stable_sort(lines.begin(), lines.end(), [](const std::vector<SlUnit>& a, const std::vector<SlUnit>& b) {
        return a.front().nb.y0 < b.front().nb.y0;
    });
    return lines;
}

// ============================================================
// 5. 8 种排版策略 (parser_*.py 的 run 方法组合)
// ============================================================



// ---- single_line (parser_single_line.py) ----
// 预处理 -> 获取每一行 -> 解包 (保持块在 blocks 中的顺序 = 行顺序)
// 注意: OcrBlock 不可删除, 故解包后需按算法结果重排 blocks。
// 为保持对外接口「原地修改 blocks」, 我们重排 blocks。
static void strategySingleLine(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    // 构造 SlUnit
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));
    // 解包: 按行顺序重排 blocks
    std::vector<OcrBlock> result;
    result.reserve(pp.tbs.size());
    for (std::vector<SlUnit>& line : lines) {
        for (SlUnit& u : line) {
            result.push_back(std::move(*u.tb));
        }
    }
    blocks.swap(result);
}

// ---- single_para (parser_single_para.py) ----
// 预处理 -> get_lines -> 行封装为 tb -> pp.run -> 解包
// 临时 tb: (nbbox, 首+尾字符, line)
static void strategySinglePara(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));

    // 将行封装为 temp_tbs
    struct TempTb {
        Nbbox nb;
        uint32_t cFirst, cLast;
        std::vector<SlUnit*> line;
    };
    std::vector<TempTb> temp_tbs;
    temp_tbs.reserve(lines.size());
    for (std::vector<SlUnit>& line : lines) {
        double b0 = line[0].nb.x0;
        double b1 = line[0].nb.y0;
        double b2 = line[0].nb.x1;
        double b3 = line[0].nb.y1;
        // 搜索 bbox (严格照搬 py:
        //   b1 = min(b1, bb[1])
        //   b2 = max(b1, bb[2])   <- 原代码用 b1, 疑似笔误, 但照搬
        //   b3 = max(b1, bb[3]))  <- 同上
        for (size_t i = 1; i < line.size(); ++i) {
            double bb1 = line[i].nb.y0; // bb[1]
            double bb2 = line[i].nb.x1; // bb[2]
            double bb3 = line[i].nb.y1; // bb[3]
            b1 = std::min(b1, bb1);
            b2 = std::max(b1, bb2); // 照搬: max(b1, bb[2])
            b3 = std::max(b1, bb3); // 照搬: max(b1, bb[3])
        }
        TempTb t;
        t.nb = {b0, b1, b2, b3};
        // text = line[0]["text"][0] + line[-1]["text"][-1]
        t.cFirst = utf8FirstCodepoint(line[0].tb->text);
        t.cLast = utf8LastCodepoint(line.back().tb->text);
        for (SlUnit& u : line) t.line.push_back(&u);
        temp_tbs.push_back(std::move(t));
    }

    // 段内分析器: 句柄用索引 (编码进 void*, 解码回 size_t)
    auto get_info = [&temp_tbs](void* handle) -> std::tuple<Nbbox, uint32_t, uint32_t> {
        size_t idx = (size_t)(uintptr_t)handle;
        TempTb& t = temp_tbs[idx];
        return std::make_tuple(t.nb, t.cFirst, t.cLast);
    };
    auto set_end = [&temp_tbs](void* handle, const std::string& end) {
        size_t idx = (size_t)(uintptr_t)handle;
        temp_tbs[idx].line.back()->tb->end = end; // 写入行尾块
    };
    ParagraphParse pp_parser(get_info, set_end);
    // 构造句柄列表 (索引编码为 void*)
    std::vector<void*> handles;
    handles.reserve(temp_tbs.size());
    for (size_t i = 0; i < temp_tbs.size(); ++i) handles.push_back((void*)(uintptr_t)i);
    pp_parser.run(handles);

    // 解包: 按行顺序重排 blocks
    std::vector<OcrBlock> result;
    result.reserve(pp.tbs.size());
    for (TempTb& t : temp_tbs) {
        for (SlUnit* u : t.line) {
            result.push_back(std::move(*u->tb));
        }
    }
    blocks.swap(result);
}

// ---- single_none (parser_single_none.py) ----
static void strategySingleNone(std::vector<OcrBlock>& blocks) {
    strategySingleLine(blocks); // super().run
    // 找到换行符, 更改为间隔符
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        if (blocks[i].end == "\n") {
            uint32_t letter1 = utf8LastCodepoint(blocks[i].text);
            uint32_t letter2 = utf8FirstCodepoint(blocks[i + 1].text);
            blocks[i].end = wordSeparator(letter1, letter2);
        }
    }
}

// ---- single_code (parser_single_code.py) ----
// merge_line: 合并一行 (用 box 4 点)
// indent: 分析所有行, 构造缩进
static OcrBlock singleCodeMergeLine(std::vector<SlUnit>& line) {
    OcrBlock A = *line[0].tb; // 拷贝
    // ba = A.box (8 int)
    double ba[8];
    for (int k = 0; k < 8; ++k) ba[k] = (double)A.box[k];
    double ha = ba[7] - ba[1]; // box[3][1] - box[0][1] = 顶点3.y - 顶点0.y
    double score = A.score;
    for (size_t i = 1; i < line.size(); ++i) {
        OcrBlock& B = *line[i].tb;
        double bb[8];
        for (int k = 0; k < 8; ++k) bb[k] = (double)B.box[k];
        ha = (ha + (bb[7] - bb[1])) / 2.0;
        // 合并文字, 补充与间距相同的空格数 (python: "  " * space)
        int space = 0;
        if (bb[0] > ba[2]) { // bb[0][0] > ba[1][0]
            space = (int)std::lround((bb[0] - ba[2]) / ha);
        }
        if (space < 0) space = 0;
        A.text += std::string((size_t)space * 2, ' ') + B.text;
        // 合并包围盒 (4 点: 0=TL,1=TR,2=BR,3=BL)
        double yTop = std::min(std::min(ba[1], ba[3]), std::min(bb[1], bb[3]));
        double yBottom = std::max(std::max(ba[5], ba[7]), std::max(bb[5], bb[7]));
        double xLeft = std::min(std::min(ba[0], ba[6]), std::min(bb[0], bb[6]));
        double xRight = std::max(std::max(ba[2], ba[4]), std::max(bb[2], bb[4]));
        ba[1] = yTop;  ba[3] = yTop;   // y 上 (TL.y, TR.y)
        ba[5] = yBottom; ba[7] = yBottom; // y 下 (BR.y, BL.y)
        ba[0] = xLeft; ba[6] = xLeft;  // x 左 (TL.x, BL.x)
        ba[2] = xRight; ba[4] = xRight; // x 右 (TR.x, BR.x)
        score += B.score;
    }
    A.score = score / (float)line.size();
    A.end = "\n";
    // 写回 box (取整, 原 py box 为 int)
    for (int k = 0; k < 8; ++k) A.box[k] = (int)ba[k];
    return A;
}

// bisect_left 在 levelList 中找插入点
static size_t bisectLeft(const std::vector<double>& a, double x) {
    size_t lo = 0, hi = a.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] < x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void singleCodeIndent(std::vector<OcrBlock>& tbs) {
    if (tbs.empty()) return;
    double lh = 0;           // 平均行高
    double xMin = std::numeric_limits<double>::infinity();
    double xMax = -std::numeric_limits<double>::infinity();
    for (OcrBlock& tb : tbs) {
        // b = tb.box
        lh += (double)tb.box[7] - (double)tb.box[1]; // box[3][1] - box[0][1]
        double x = (double)tb.box[0];                 // box[0][0]
        if (x < xMin) xMin = x;
        if (x > xMax) xMax = x;
    }
    lh /= (double)tbs.size();
    double lh2 = lh / 2.0;
    // 构建缩进层级列表
    std::vector<double> levelList;
    double x = xMin;
    while (x < xMax) {
        levelList.push_back(x);
        x += lh;
    }
    // 按照层级, 为每行句首加上空格, 并调整包围盒
    for (OcrBlock& tb : tbs) {
        // level = bisect_left(levelList, b[0][0] + lh2) - 1
        size_t pos = bisectLeft(levelList, (double)tb.box[0] + lh2);
        long level = (long)pos - 1;
        if (level < 0) level = 0;
        tb.text = std::string((size_t)level * 2, ' ') + tb.text;
        tb.box[0] = (int)xMin; tb.box[6] = (int)xMin; // 左侧归零 (TL.x, BL.x)
    }
}

static void strategySingleCode(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));
    // 合并所有行
    std::vector<OcrBlock> tbsOut;
    tbsOut.reserve(lines.size());
    for (std::vector<SlUnit>& line : lines) {
        tbsOut.push_back(singleCodeMergeLine(line));
    }
    // 为每行添加句首缩进
    singleCodeIndent(tbsOut);
    blocks.swap(tbsOut);
}

// ---- single_vertical (古籍竖排) ----
// 将每个块的 bbox 逆时针旋转 90° (模拟图像旋转), 然后直接复用 single_line 逻辑。
// 原理: 竖排文字旋转 90° CCW 后, 右列变顶行, 行内从左到右 = 原从上到下,
//        与横排阅读顺序一致, 可直接套用 single_line 的分组 + 排序算法。
static void strategySingleVertical(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }

    // 计算页面中心 (用于旋转)
    double pageW = 0, pageH = 0;
    for (const Nbbox& nb : pp.nbboxes) {
        if (nb.x1 > pageW) pageW = nb.x1;
        if (nb.y1 > pageH) pageH = nb.y1;
    }
    double cx = pageW / 2.0, cy = pageH / 2.0;

    // 将每个块的 nbbox 逆时针旋转 90°, 同步旋转 box[8]
    // CCW 90°: (x,y) -> (cy + y - cy, cx - x + cx) = (y + cx - cy, cx + cy - x)
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        Nbbox& nb = pp.nbboxes[i];
        OcrBlock* tb = pp.tbs[i];

        // 旋转 nbbox (4 点: TL,TR,BR,BL)
        double pts[4][2] = {
            {nb.x0, nb.y0}, {nb.x1, nb.y0},
            {nb.x1, nb.y1}, {nb.x0, nb.y1}
        };
        double rx[4], ry[4];
        for (int k = 0; k < 4; ++k) {
            rx[k] = pts[k][1] + cx - cy;
            ry[k] = cx + cy - pts[k][0];
        }
        nb.x0 = *std::min_element(rx, rx + 4);
        nb.y0 = *std::min_element(ry, ry + 4);
        nb.x1 = *std::max_element(rx, rx + 4);
        nb.y1 = *std::max_element(ry, ry + 4);

        // 旋转 box[8] (4 点: [x0,y0, x1,y1, x2,y2, x3,y3] = TL,TR,BR,BL)
        for (int k = 0; k < 4; ++k) {
            double bx = (double)tb->box[2 * k];
            double by = (double)tb->box[2 * k + 1];
            tb->box[2 * k]     = (int)std::lround(by + cx - cy);
            tb->box[2 * k + 1] = (int)std::lround(cx + cy - bx);
        }
    }

    // 直接复用 single_line: 旋转后的竖排 = 横排
    strategySingleLine(blocks);
}

// ---- multi_line (parser_multi_line.py) ----
// 优化: GapTree 划分区块后，对每个区块内部应用 FineSort 精细排序
static void strategyMultiLine(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    // get_bbox = tb["normalized_bbox"]; 用 nbMap 把指针映射回顾一化 bbox
    std::unordered_map<OcrBlock*, Nbbox> nbMap;
    nbMap.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) nbMap[pp.tbs[i]] = pp.nbboxes[i];
    GapTree gtree([&nbMap](OcrBlock* tb) -> Nbbox { return nbMap.at(tb); });
    
    // GapTree 划分区块
    std::vector<OcrBlock*> sorted = gtree.sort(pp.tbs);
    std::vector<std::vector<OcrBlock*>> nodes = gtree.getNodesTextBlocks();
    
    // 对每个区块内部应用 FineSort 精细排序
    FineSort::Params fine_params;
    std::vector<OcrBlock*> final_sorted;
    for (std::vector<OcrBlock*>& region_blocks : nodes) {
        if (region_blocks.size() > 1) {
            // 区块内有多个文本块，应用精细排序
            std::vector<OcrBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
            final_sorted.insert(final_sorted.end(), region_sorted.begin(), region_sorted.end());
        } else {
            // 区块内只有一个文本块，直接添加
            final_sorted.insert(final_sorted.end(), region_blocks.begin(), region_blocks.end());
        }
    }
    
    // 补充行尾间隔符
    std::vector<OcrBlock> result;
    result.reserve(final_sorted.size());
    for (OcrBlock* tb : final_sorted) {
        tb->end = "\n";
        result.push_back(std::move(*tb));
    }
    blocks.swap(result);
}

// ---- multi_para (parser_multi_para.py) ----
// 优化: GapTree 划分区块后，对每个区块内部应用 FineSort 精细排序，再进行段落分析
static void strategyMultiPara(std::vector<OcrBlock>& blocks) {
    try {
        std::cerr << "[layout] strategyMultiPara start: blocks=" << blocks.size() << std::endl;
        
        Preprocessed pp = linePreprocessing(blocks);
        std::cerr << "[layout] linePreprocessing done: tbs=" << pp.tbs.size() 
                  << ", nbboxes=" << pp.nbboxes.size() << std::endl;
        
        if (pp.tbs.empty()) { 
            std::cerr << "[layout] pp.tbs empty, clear blocks" << std::endl;
            blocks.clear(); 
            return; 
        }
        
        std::unordered_map<OcrBlock*, Nbbox> nbMap;
        nbMap.reserve(pp.tbs.size());
        for (size_t i = 0; i < pp.tbs.size(); ++i) {
            nbMap[pp.tbs[i]] = pp.nbboxes[i];
        }
        std::cerr << "[layout] nbMap created: size=" << nbMap.size() << std::endl;
        
        GapTree gtree([&nbMap](OcrBlock* tb) -> Nbbox { 
            try {
                return nbMap.at(tb);
            } catch (const std::exception& e) {
                std::cerr << "[layout] ERROR in GapTree get_bbox: " << e.what() << std::endl;
                throw;
            }
        });
        std::cerr << "[layout] GapTree created" << std::endl;
        
        // GapTree 划分区块
        std::cerr << "[layout] calling GapTree.sort..." << std::endl;
        std::vector<OcrBlock*> sorted = gtree.sort(pp.tbs);
        std::cerr << "[layout] GapTree.sort done: sorted.size=" << sorted.size() << std::endl;
        
        std::cerr << "[layout] calling GapTree.getNodesTextBlocks..." << std::endl;
        std::vector<std::vector<OcrBlock*>> nodes = gtree.getNodesTextBlocks();
        std::cerr << "[layout] GapTree.getNodesTextBlocks done: nodes.size=" << nodes.size() << std::endl;
        
        // 对每个区块内部应用 FineSort 精细排序
        FineSort::Params fine_params;
        std::vector<std::vector<OcrBlock*>> sorted_nodes;
        std::cerr << "[layout] processing nodes with FineSort..." << std::endl;
        
        for (size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
            std::vector<OcrBlock*>& region_blocks = nodes[nodeIdx];
            std::cerr << "[layout] node[" << nodeIdx << "]: region_blocks.size=" << region_blocks.size() << std::endl;
            
            if (region_blocks.size() > 1) {
                // 区块内有多个文本块，应用精细排序
                std::cerr << "[layout] calling FineSort.sortRegion for node[" << nodeIdx << "]..." << std::endl;
                try {
                    std::vector<OcrBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
                    std::cerr << "[layout] FineSort.sortRegion done: region_sorted.size=" << region_sorted.size() << std::endl;
                    sorted_nodes.push_back(std::move(region_sorted));
                } catch (const std::exception& e) {
                    std::cerr << "[layout] FineSort.sortRegion FAILED for node[" << nodeIdx << "]: " << e.what() << std::endl;
                    // FineSort失败，使用原始排序
                    sorted_nodes.push_back(std::move(region_blocks));
                }
            } else {
                // 区块内只有一个文本块，直接添加
                std::cerr << "[layout] node[" << nodeIdx << "] has only 1 block, skip FineSort" << std::endl;
                sorted_nodes.push_back(std::move(region_blocks));
            }
        }
        std::cerr << "[layout] FineSort processing done: sorted_nodes.size=" << sorted_nodes.size() << std::endl;

        // 段内分析器: get_info 返回 (nbbox, 首/尾码点), set_end 写 tb->end
        // 注意: lambda 捕获 nbMap 的引用, 确保 nbMap 在 ParagraphParse.run() 完成前有效
        auto get_info = [&nbMap](void* handle) -> std::tuple<Nbbox, uint32_t, uint32_t> {
            try {
                std::cerr << "[layout] get_info lambda: handle=" << handle << std::endl;
                
                OcrBlock* tb = static_cast<OcrBlock*>(handle);
                std::cerr << "[layout] get_info lambda: tb=" << tb << ", tb->text='" << tb->text << "'" << std::endl;
                
                std::cerr << "[layout] get_info lambda: accessing nbMap..." << std::endl;
                Nbbox nb = nbMap.at(tb);
                std::cerr << "[layout] get_info lambda: nbMap.at() returned: x0=" << nb.x0 
                          << ", y0=" << nb.y0 << ", x1=" << nb.x1 << ", y1=" << nb.y1 << std::endl;
                
                std::cerr << "[layout] get_info lambda: calling utf8FirstCodepoint..." << std::endl;
                uint32_t first = utf8FirstCodepoint(tb->text);
                std::cerr << "[layout] get_info lambda: utf8FirstCodepoint returned: " << first << std::endl;
                
                std::cerr << "[layout] get_info lambda: calling utf8LastCodepoint..." << std::endl;
                uint32_t last = utf8LastCodepoint(tb->text);
                std::cerr << "[layout] get_info lambda: utf8LastCodepoint returned: " << last << std::endl;
                
                std::cerr << "[layout] get_info lambda: creating tuple..." << std::endl;
                auto result = std::make_tuple(nb, first, last);
                std::cerr << "[layout] get_info lambda: tuple created, returning" << std::endl;
                
                return result;
            } catch (const std::exception& e) {
                std::cerr << "[layout] ERROR in get_info lambda: " << e.what() << std::endl;
                throw;
            } catch (...) {
                std::cerr << "[layout] UNKNOWN ERROR in get_info lambda" << std::endl;
                throw;
            }
        };
        
        auto set_end = [](void* handle, const std::string& end) {
            try {
                std::cerr << "[layout] set_end lambda: handle=" << handle << ", end='" << end << "'" << std::endl;
                
                OcrBlock* tb = static_cast<OcrBlock*>(handle);
                std::cerr << "[layout] set_end lambda: tb=" << tb << std::endl;
                
                tb->end = end;
                std::cerr << "[layout] set_end lambda: tb->end set successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[layout] ERROR in set_end lambda: " << e.what() << std::endl;
                throw;
            } catch (...) {
                std::cerr << "[layout] UNKNOWN ERROR in set_end lambda" << std::endl;
                throw;
            }
        };
        
        std::cerr << "[layout] creating ParagraphParse..." << std::endl;
        ParagraphParse pp_parser(get_info, set_end);
        std::cerr << "[layout] ParagraphParse created" << std::endl;

        // 对每个节点, 进行自然段分析
        std::cerr << "[layout] running ParagraphParse for each node..." << std::endl;
        for (size_t nodeIdx = 0; nodeIdx < sorted_nodes.size(); ++nodeIdx) {
            std::vector<OcrBlock*>& tbs = sorted_nodes[nodeIdx];
            std::cerr << "[layout] ParagraphParse node[" << nodeIdx << "]: tbs.size=" << tbs.size() << std::endl;
            
            std::vector<void*> handles;
            handles.reserve(tbs.size());
            for (OcrBlock* tb : tbs) handles.push_back(tb);
            
            try {
                pp_parser.run(handles);
                std::cerr << "[layout] ParagraphParse node[" << nodeIdx << "] done" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[layout] ParagraphParse FAILED for node[" << nodeIdx << "]: " << e.what() << std::endl;
                // ParagraphParse失败，设置默认end
                for (OcrBlock* tb : tbs) {
                    tb->end = "\n";
                }
            }
        }
        
        // 输出: 按 sorted_nodes 顺序重排
        std::cerr << "[layout] building result..." << std::endl;
        std::vector<OcrBlock> result;
        for (std::vector<OcrBlock*>& tbs : sorted_nodes) {
            for (OcrBlock* tb : tbs) {
                result.push_back(std::move(*tb));
            }
        }
        blocks.swap(result);
        
        std::cerr << "[layout] strategyMultiPara success: output blocks=" << blocks.size() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[layout] EXCEPTION in strategyMultiPara: " << e.what() << std::endl;
        throw;
    } catch (...) {
        std::cerr << "[layout] UNKNOWN EXCEPTION in strategyMultiPara" << std::endl;
        throw;
    }
}

// ---- multi_none (parser_multi_none.py) ----
// 优化: GapTree 划分区块后，对每个区块内部应用 FineSort 精细排序
static void strategyMultiNone(std::vector<OcrBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::unordered_map<OcrBlock*, Nbbox> nbMap;
    nbMap.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) nbMap[pp.tbs[i]] = pp.nbboxes[i];
    GapTree gtree([&nbMap](OcrBlock* tb) -> Nbbox { return nbMap.at(tb); });
    
    // GapTree 划分区块
    std::vector<OcrBlock*> sorted = gtree.sort(pp.tbs);
    std::vector<std::vector<OcrBlock*>> nodes = gtree.getNodesTextBlocks();
    
    // 对每个区块内部应用 FineSort 精细排序
    FineSort::Params fine_params;
    std::vector<OcrBlock*> final_sorted;
    for (std::vector<OcrBlock*>& region_blocks : nodes) {
        if (region_blocks.size() > 1) {
            // 区块内有多个文本块，应用精细排序
            std::vector<OcrBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
            final_sorted.insert(final_sorted.end(), region_sorted.begin(), region_sorted.end());
        } else {
            // 区块内只有一个文本块，直接添加
            final_sorted.insert(final_sorted.end(), region_blocks.begin(), region_blocks.end());
        }
    }
    
    // 补充行尾间隔符
    std::vector<OcrBlock> result;
    result.reserve(final_sorted.size());
    for (size_t i = 0; i < final_sorted.size(); ++i) {
        OcrBlock* tb = final_sorted[i];
        if (i < final_sorted.size() - 1) {
            OcrBlock* next = final_sorted[i + 1];
            tb->end = wordSeparatorText(tb->text, next->text);
        } else {
            tb->end = "\n";
        }
        result.push_back(std::move(*tb));
    }
    blocks.swap(result);
}

// ============================================================
// 对外接口 (tbpu/__init__.py 的 getParser 分发)
// ============================================================

void applyLayout(std::vector<OcrBlock>& blocks, const std::string& strategyKey) {
    try {
        // 调试日志：输入信息
        std::cerr << "[layout] applyLayout start: blocks=" << blocks.size() 
                  << ", strategy=" << strategyKey << std::endl;
        
        if (blocks.empty()) {
            std::cerr << "[layout] blocks empty, skip" << std::endl;
            return;
        }
        
        // 检查每个块的数据完整性
        for (size_t i = 0; i < blocks.size(); ++i) {
            const OcrBlock& b = blocks[i];
            std::cerr << "[layout] block[" << i << "]: text='" << b.text 
                      << "', box.size=" << b.box.size() 
                      << ", score=" << b.score << std::endl;
            
            // 验证 box 数据
            if (b.box.size() != 8) {
                std::cerr << "[layout] ERROR: block[" << i << "] has invalid box size: " 
                          << b.box.size() << " (expected 8)" << std::endl;
                // 修复：如果 box 不完整，跳过这个块
                continue;
            }
            
            // 验证坐标合理性
            for (int j = 0; j < 8; ++j) {
                if (b.box[j] < 0 || b.box[j] > 10000) {
                    std::cerr << "[layout] WARNING: block[" << i << "] box[" << j 
                              << "] has unusual value: " << b.box[j] << std::endl;
                }
            }
        }
        
        std::cerr << "[layout] calling strategy function..." << std::endl;
        
        if (strategyKey == "multi_para") {
            std::cerr << "[layout] strategy: multi_para" << std::endl;
            strategyMultiPara(blocks);
        } else if (strategyKey == "multi_line") {
            std::cerr << "[layout] strategy: multi_line" << std::endl;
            strategyMultiLine(blocks);
        } else if (strategyKey == "multi_none") {
            std::cerr << "[layout] strategy: multi_none" << std::endl;
            strategyMultiNone(blocks);
        } else if (strategyKey == "single_para") {
            std::cerr << "[layout] strategy: single_para" << std::endl;
            strategySinglePara(blocks);
        } else if (strategyKey == "single_line") {
            std::cerr << "[layout] strategy: single_line" << std::endl;
            strategySingleLine(blocks);
        } else if (strategyKey == "single_none") {
            std::cerr << "[layout] strategy: single_none" << std::endl;
            strategySingleNone(blocks);
        } else if (strategyKey == "single_code") {
            std::cerr << "[layout] strategy: single_code" << std::endl;
            strategySingleCode(blocks);
        } else if (strategyKey == "single_vertical") {
            std::cerr << "[layout] strategy: single_vertical" << std::endl;
            strategySingleVertical(blocks);
        } else {
            std::cerr << "[layout] WARNING: unknown strategy '" << strategyKey 
                      << "', fallback to multi_para" << std::endl;
            strategyMultiPara(blocks);
        }
        
        std::cerr << "[layout] applyLayout success, output blocks=" << blocks.size() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[layout] EXCEPTION in applyLayout: " << e.what() << std::endl;
        std::cerr << "[layout] strategy=" << strategyKey << ", blocks=" << blocks.size() << std::endl;
        // 异常时不清空 blocks，保持原始数据
        throw;
    } catch (...) {
        std::cerr << "[layout] UNKNOWN EXCEPTION in applyLayout" << std::endl;
        std::cerr << "[layout] strategy=" << strategyKey << ", blocks=" << blocks.size() << std::endl;
        throw;
    }
}
