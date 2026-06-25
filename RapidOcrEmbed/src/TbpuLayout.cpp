// TBPU 排版算法 C++ 实现 (适配 TextBlock)
// 混合了 Umi-OCR 的 GapTree 多栏版面分析算法和 k2pdfopt 的精细排序算法
// Umi-OCR 项目: https://github.com/hiroi-sora/Umi-OCR
// k2pdfopt 项目: http://willus.com/k2pdfopt/

#include "TbpuLayout.h"

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

// 旋转角度阈值 (度)
const double TBPU_ANGLE_THRESHOLD = 3.0;
// 段落对齐/行间距对比阈值
const double TBPU_TH = 1.2;

// 排版策略信息列表 (7 个策略)
const std::vector<LayoutStrategyInfo>& layoutStrategies() {
    static std::vector<LayoutStrategyInfo> strategies = {
        {"multi_para", "Multi-Para", "Multi-column, merge paragraphs"},
        {"multi_line", "Multi-Line", "Multi-column, each line"},
        {"multi_none", "Multi-None", "Multi-column, continuous"},
        {"single_para", "Single-Para", "Single-column, merge paragraphs"},
        {"single_line", "Single-Line", "Single-column, each line"},
        {"single_none", "Single-None", "Single-column, continuous"},
        {"single_code", "Single-Code", "Single-column, preserve indent"},
        {"single_vertical", "Single-Vertical", "Single-column, vertical reading (right-to-left, top-to-bottom)"}
    };
    return strategies;
}

int layoutStrategyCount() {
    return (int)layoutStrategies().size();
}

int layoutStrategyIndex(const std::string& key) {
    const auto& strategies = layoutStrategies();
    for (int i = 0; i < (int)strategies.size(); ++i) {
        if (key == strategies[i].key) return i;
    }
    return -1;
}

const char* layoutStrategyKey(int index) {
    const auto& strategies = layoutStrategies();
    if (index < 0 || index >= (int)strategies.size()) return "";
    return strategies[index].key;
}

// ============================================================
// 内部工具: Unicode / 字符串
// ============================================================

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
    else { cp = c; extra = 0; }
    for (int i = 1; i <= extra && (size_t)i < s.size(); ++i) {
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    return cp;
}

static uint32_t utf8LastCodepoint(const std::string& s) {
    if (s.empty()) return 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = s.size();
    while (i > 1 && (p[i - 1] & 0xC0) == 0x80) {
        --i;
    }
    --i;
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

static bool isCjk(uint32_t cp) {
    static const std::pair<uint32_t, uint32_t> ranges[] = {
        {0x4E00, 0x9FFF}, {0x3040, 0x30FF}, {0x1100, 0x11FF},
        {0x3130, 0x318F}, {0xAC00, 0xD7AF}, {0x3000, 0x303F},
        {0xFE30, 0xFE4F}, {0xFF00, 0xFFEF},
    };
    for (const auto& pr : ranges) {
        if (cp >= pr.first && cp <= pr.second) return true;
    }
    return false;
}

static bool isPunct(uint32_t cp) {
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
    if (cp == 0x00A1 || cp == 0x00A7 || cp == 0x00AB || cp == 0x00B6 ||
        cp == 0x00B7 || cp == 0x00BB || cp == 0x00BF) return true;
    if (cp >= 0x2010 && cp <= 0x2027) return true;
    if (cp >= 0x2030 && cp <= 0x205E) return true;
    if (cp >= 0x2080 && cp <= 0x208E) return true;
    if (cp == 0x2E2E || cp == 0x2E3A || cp == 0x2E3B) return true;
    if (cp >= 0x2768 && cp <= 0x2775) return true;
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
    if (cp >= 0xFE10 && cp <= 0xFE19) return true;
    if (cp >= 0xFE41 && cp <= 0xFE44) return true;
    if (cp >= 0xFE47 && cp <= 0xFE48) return true;
    if (cp >= 0xFE56 && cp <= 0xFE5A) return true;
    if (cp >= 0xFE63 && cp <= 0xFE63) return true;
    if (cp >= 0xFE68 && cp <= 0xFE68) return true;
    if (cp >= 0xFE6A && cp <= 0xFE6B) return true;
    if (cp == 0xFF61) return true;
    return false;
}

static std::string wordSeparator(uint32_t cp1, uint32_t cp2) {
    if (isCjk(cp1) && isCjk(cp2)) return "";
    if (cp1 == '-') return "";
    if (isPunct(cp2)) return "";
    return " ";
}

static std::string wordSeparatorText(const std::string& letter1Text, const std::string& letter2Text) {
    return wordSeparator(utf8LastCodepoint(letter1Text), utf8FirstCodepoint(letter2Text));
}

// ============================================================
// box 访问 (适配 TextBlock: boxPoint 是 4 个 cv::Point)
// ============================================================
static inline double boxPointX(const TextBlock& b, int i) { return (double)b.boxPoint[i].x; }
static inline double boxPointY(const TextBlock& b, int i) { return (double)b.boxPoint[i].y; }

// ============================================================
// 1. line_preprocessing
// ============================================================

static inline double ppDistance(double x1, double y1, double x2, double y2) {
    return std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

static double ppCalculateAngle(const TextBlock& block) {
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

static double ppEstimateRotation(const std::vector<TextBlock*>& tbs) {
    std::vector<double> angle_rads;
    angle_rads.reserve(tbs.size());
    for (TextBlock* tb : tbs) {
        angle_rads.push_back(ppCalculateAngle(*tb));
    }
    if (angle_rads.empty()) return 0.0;
    std::sort(angle_rads.begin(), angle_rads.end());
    size_t n = angle_rads.size();
    if (n % 2 == 1) {
        return angle_rads[n / 2];
    } else {
        return (angle_rads[n / 2 - 1] + angle_rads[n / 2]) / 2.0;
    }
}

struct Nbbox { double x0, y0, x1, y1; };

static std::vector<Nbbox> ppGetBboxes(const std::vector<TextBlock*>& tbs, double rotation_rad) {
    double angle_threshold_rad = TBPU_ANGLE_THRESHOLD * M_PI / 180.0;
    std::vector<Nbbox> bboxes;
    bboxes.reserve(tbs.size());
    if (std::fabs(rotation_rad) <= angle_threshold_rad) {
        for (TextBlock* tb : tbs) {
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
        double rad = -rotation_rad;
        double cos_angle = std::cos(rad);
        double sin_angle = std::sin(rad);
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = std::numeric_limits<double>::infinity();
        for (TextBlock* tb : tbs) {
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
        if (min_x < 0 || min_y < 0) {
            for (Nbbox& b : bboxes) {
                b.x0 -= min_x; b.y0 -= min_y;
                b.x1 -= min_x; b.y1 -= min_y;
            }
        }
    }
    return bboxes;
}

struct Preprocessed {
    std::vector<TextBlock*> tbs;
    std::vector<Nbbox> nbboxes;
};

static Preprocessed linePreprocessing(std::vector<TextBlock>& blocks) {
    Preprocessed out;
    for (TextBlock& b : blocks) {
        if (!b.text.empty()) {
            out.tbs.push_back(&b);
        }
    }
    if (out.tbs.empty()) return out;

    double rotation_rad = ppEstimateRotation(out.tbs);
    std::vector<Nbbox> bboxes = ppGetBboxes(out.tbs, rotation_rad);
    out.nbboxes = std::move(bboxes);
    std::vector<size_t> idx(out.tbs.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return out.nbboxes[a].y0 < out.nbboxes[b].y0;
    });
    std::vector<TextBlock*> sortedTbs;
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
// 2. GapTree 间隙树算法
// ============================================================

struct GtUnit {
    double bbox[4];
    TextBlock* tb;
};

class GapTree {
public:
    using GetBbox = std::function<Nbbox(TextBlock*)>;

    GapTree(GetBbox get_bbox) : get_bbox_(std::move(get_bbox)) {}

    std::vector<TextBlock*> sort(const std::vector<TextBlock*>& text_blocks) {
        double page_l, page_r;
        std::vector<GtUnit> units = getUnits(text_blocks, page_l, page_r);
        auto cr = getCutsRows(units, page_l, page_r);
        const std::vector<GtUnitCut>& cuts = cr.first;
        const std::vector<std::vector<GtUnit>>& rows = cr.second;
        getLayoutTree(cuts, rows);
        std::vector<LNode*> nodes = preorderTraversal();
        current_rows_ = rows;
        current_cuts_ = cuts;
        current_nodes_.clear();
        for (LNode* n : nodes) current_nodes_.push_back(n);
        return getTextBlocks(nodes);
    }

    std::vector<std::vector<TextBlock*>> getNodesTextBlocks() const {
        std::vector<std::vector<TextBlock*>> result;
        for (LNode* node : current_nodes_) {
            if (!node->units.empty()) {
                std::vector<TextBlock*> tbs;
                for (const GtUnit& u : node->units) tbs.push_back(u.tb);
                result.push_back(std::move(tbs));
            }
        }
        return result;
    }

private:
    GetBbox get_bbox_;
    std::vector<std::vector<GtUnit>> current_rows_;
    struct GtUnitCut { double l, r; int r_start, r_end; };
    std::vector<GtUnitCut> current_cuts_;

    struct LNode {
        double x_left, x_right;
        int r_top, r_bottom;
        std::vector<GtUnit> units;
        std::vector<LNode*> children;
    };
    std::vector<LNode*> current_nodes_;
    std::vector<std::unique_ptr<LNode>> owned_nodes_;

    std::vector<GtUnit> getUnits(const std::vector<TextBlock*>& text_blocks, double& page_l, double& page_r) {
        std::vector<GtUnit> units;
        page_l = std::numeric_limits<double>::infinity();
        page_r = -1;
        for (TextBlock* tb : text_blocks) {
            Nbbox b = get_bbox_(tb);
            GtUnit u;
            u.bbox[0] = b.x0; u.bbox[1] = b.y0; u.bbox[2] = b.x1; u.bbox[3] = b.y1;
            u.tb = tb;
            units.push_back(u);
            if (b.x0 < page_l) page_l = b.x0;
            if (b.x1 > page_r) page_r = b.x1;
        }
        std::stable_sort(units.begin(), units.end(), [](const GtUnit& a, const GtUnit& b) {
            return a.bbox[1] < b.bbox[1];
        });
        return units;
    }

    struct Gap { double l, r; int start_row; };
    static std::pair<std::vector<Gap>, std::vector<Gap>> updateGaps(const std::vector<Gap>& gaps1,
                                                                    const std::vector<Gap>& gaps2) {
        std::vector<char> flags1(gaps1.size(), 1);
        std::vector<char> flags2(gaps2.size(), 1);
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

    std::pair<std::vector<GtUnitCut>, std::vector<std::vector<GtUnit>>> getCutsRows(
        const std::vector<GtUnit>& units, double page_l, double page_r) {
        page_l -= 1;
        page_r += 1;
        std::vector<std::vector<GtUnit>> rows;
        std::vector<GtUnitCut> completed_cuts;
        std::vector<Gap> gaps;
        int row_index = 0;
        size_t unit_index = 0;
        size_t l_units = units.size();
        while (unit_index < l_units) {
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
            auto ug = updateGaps(gaps, row_gaps);
            gaps = ug.first;
            const std::vector<Gap>& del_gaps = ug.second;
            int row_max = row_index - 1;
            for (const Gap& dg1 : del_gaps) {
                completed_cuts.push_back({dg1.l, dg1.r, dg1.start_row, row_max});
            }
            rows.push_back(row);
            unit_index += 1;
            row_index += 1;
        }
        int row_max = (int)rows.size() - 1;
        for (const Gap& g : gaps) {
            completed_cuts.push_back({g.l, g.r, g.start_row, row_max});
        }
        std::sort(completed_cuts.begin(), completed_cuts.end(),
                  [](const GtUnitCut& a, const GtUnitCut& b) { return a.l < b.l; });
        return {completed_cuts, rows};
    }

    LNode* newLNode() {
        owned_nodes_.push_back(std::unique_ptr<LNode>(new LNode()));
        return owned_nodes_.back().get();
    }

    void getLayoutTree(const std::vector<GtUnitCut>& cuts,
                       const std::vector<std::vector<GtUnit>>& rows) {
        std::vector<std::vector<std::pair<double, double>>> rows_gaps(rows.size());
        for (const GtUnitCut& cut : cuts) {
            for (int r_i = cut.r_start; r_i <= cut.r_end; ++r_i) {
                rows_gaps[r_i].push_back({cut.l, cut.r});
            }
        }

        LNode* rootPtr = newLNode();
        rootPtr->x_left = cuts[0].l - 1;
        rootPtr->x_right = cuts.back().r + 1;
        rootPtr->r_top = -1;
        rootPtr->r_bottom = -1;
        rootPtr->units.clear();
        rootPtr->children.clear();

        std::vector<LNode*> completed_nodes;
        std::vector<LNode*> now_nodes;
        completed_nodes.push_back(rootPtr);

        auto complete = [&](LNode* node) {
            double node_r = node->x_right - 2;
            std::vector<LNode*> max_nodes;
            int max_r = -2;
            for (LNode* com_node : completed_nodes) {
                if (node_r < com_node->x_left || node_r > com_node->x_right + 0.0001) continue;
                if (com_node->r_bottom >= node->r_top) continue;
                if (com_node->r_bottom > max_r) {
                    max_r = com_node->r_bottom;
                    max_nodes = {com_node};
                    continue;
                }
                if (com_node->r_bottom == max_r) {
                    max_nodes.push_back(com_node);
                    continue;
                }
            }
            LNode* max_node = nullptr;
            double best_xr = -std::numeric_limits<double>::infinity();
            for (LNode* n : max_nodes) {
                if (n->x_right > best_xr) { best_xr = n->x_right; max_node = n; }
            }
            max_node->children.push_back(node);
            completed_nodes.push_back(node);
        };

        for (size_t r_i_sz = 0; r_i_sz < rows.size(); ++r_i_sz) {
            int r_i = (int)r_i_sz;
            const std::vector<GtUnit>& row = rows[r_i_sz];
            const std::vector<std::pair<double, double>>& row_gaps = rows_gaps[r_i_sz];
            size_t u_i = 0, g_i = 0;

            std::vector<LNode*> new_nodes;
            for (LNode* node : now_nodes) {
                bool l_flag = false, r_flag = false;
                bool completed_flag = false;
                double x_left = node->x_left, x_right = node->x_right;
                for (const std::pair<double, double>& gap : row_gaps) {
                    if (gap.second == x_left) l_flag = true;
                    if (gap.first == x_right) r_flag = true;
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

            while (u_i < row.size()) {
                const GtUnit& unit = row[u_i];
                double x_l = row_gaps[g_i].second;
                double x_r = row_gaps[g_i + 1].first;
                if (unit.bbox[0] + 0.0001 > x_r) {
                    g_i += 1;
                    continue;
                }
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
        for (LNode* node : now_nodes) {
            complete(node);
        }
        for (LNode* node : completed_nodes) {
            std::sort(node->children.begin(), node->children.end(),
                      [](LNode* a, LNode* b) { return a->x_left < b->x_left; });
            std::sort(node->units.begin(), node->units.end(),
                      [](const GtUnit& a, const GtUnit& b) { return a.bbox[1] < b.bbox[1]; });
        }
    }

    std::vector<LNode*> preorderTraversal() {
        std::vector<LNode*> result;
        if (owned_nodes_.empty()) return result;
        LNode* rootPtr = owned_nodes_[0].get();
        std::vector<LNode*> stack;
        stack.push_back(rootPtr);
        while (!stack.empty()) {
            LNode* node = stack.back();
            stack.pop_back();
            result.push_back(node);
            for (size_t i = node->children.size(); i-- > 0;) {
                stack.push_back(node->children[i]);
            }
        }
        return result;
    }

    std::vector<TextBlock*> getTextBlocks(const std::vector<LNode*>& nodes) const {
        std::vector<TextBlock*> result;
        for (LNode* node : nodes) {
            for (const GtUnit& unit : node->units) {
                result.push_back(unit.tb);
            }
        }
        return result;
    }
};

// ============================================================
// 3. FineSort 精细排序算法
// ============================================================

class FineSort {
public:
    struct Params {
        double col_misalign_max = 0.3;
        double row_gap_min_ratio = 0.1;
        double max_col_gap_ratio = 10.0;
        double font_size_epsilon_ratio = 0.15;
        bool use_font_info = true;
        bool left_to_right = true;
    };

    struct NormBox {
        double x1, y1, x2, y2;
        double font_size;
        double baseline;
        std::string text;
        TextBlock* tb;
        int next = -1;
        int prev = -1;
        bool ignore = false;
    };

    static std::vector<TextBlock*> sortRegion(
        const std::vector<TextBlock*>& region_blocks,
        const std::unordered_map<TextBlock*, Nbbox>& nbMap,
        const Params& params = Params()) {

        if (region_blocks.size() < 2) {
            return region_blocks;
        }

        std::vector<NormBox> boxes = convertToNormBoxes(region_blocks, nbMap);

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

        std::vector<int> sorted_indices = sortRegionsFull(
            boxes, params, avg_font_height, avg_char_width);

        std::vector<TextBlock*> result;
        result.reserve(sorted_indices.size());
        for (int idx : sorted_indices) {
            result.push_back(boxes[idx].tb);
        }
        return result;
    }

private:
    static std::vector<NormBox> convertToNormBoxes(
        const std::vector<TextBlock*>& region_blocks,
        const std::unordered_map<TextBlock*, Nbbox>& nbMap) {

        std::vector<NormBox> boxes;
        boxes.reserve(region_blocks.size());

        for (TextBlock* tb : region_blocks) {
            Nbbox nb = nbMap.at(tb);
            NormBox box;
            box.x1 = nb.x0;
            box.y1 = nb.y0;
            box.x2 = nb.x1;
            box.y2 = nb.y1;
            box.text = tb->text;
            box.tb = tb;

            double height = nb.y1 - nb.y0;
            box.font_size = height;
            box.baseline = nb.y1 - height * 0.1;

            boxes.push_back(box);
        }
        return boxes;
    }

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

    static std::tuple<double, double, double> determineCloseness(
        const NormBox& box0, const NormBox& box1) {

        double col_align = alignment(box0.x1, box0.x2, box1.x1, box1.x2);
        double row_align = alignment(box0.y1, box0.y2, box1.y1, box1.y2);

        double gap;
        if (col_align > 0) {
            gap = box1.y1 - box0.y2;
        } else {
            gap = box1.x1 - box0.x2;
        }
        return std::make_tuple(gap, col_align, row_align);
    }

    static double positionCompare(const NormBox& box1, const NormBox& box2) {
        bool vertical_overlap = (box2.y2 > box1.y1 && box2.y1 < box1.y2);
        bool horizontal_overlap = (box2.x2 > box1.x1 && box2.x1 < box1.x2);

        if (vertical_overlap) return box1.x1 - box2.x1;
        if (horizontal_overlap) return box1.y1 - box2.y1;
        return (box1.x1 + box1.y1) - (box2.x1 + box2.y1);
    }

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

    static bool trappedBox(int ibox, std::vector<NormBox>& boxes, int n, double comax) {
        if (comax > 0.2) comax = 0.2;

        std::vector<int> trapped(n, 0);

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

        int idx = first;
        while (idx >= 0 && idx < n) {
            trapped[idx] = 255;
            idx = boxes[idx].next;
        }

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

                if (boxes[i].y2 <= box.y1) {
                    if (boxes[i].x2 >= ibx1 && boxes[i].x1 <= ibx2) trapped[i] |= 1;
                }
                if (boxes[i].y1 >= box.y2) {
                    if (boxes[i].x2 >= ibx1 && boxes[i].x1 <= ibx2) trapped[i] |= 8;
                }
                if (boxes[i].x2 <= box.x1) {
                    if (boxes[i].y1 <= iby2 && boxes[i].y2 >= iby1) trapped[i] |= 2;
                }
                if (boxes[i].x1 >= box.x2) {
                    if (boxes[i].y1 <= iby2 && boxes[i].y2 >= iby1) trapped[i] |= 4;
                }
            }
            ibox_iter = boxes[ibox_iter].next;
        }

        for (int i = 0; i < n; ++i) {
            if (trapped[i] != 255) {
                if ((trapped[i] & 3) == 3 || (trapped[i] & 5) == 5 || (trapped[i] & 10) == 10) {
                    return true;
                }
            }
        }
        return false;
    }

    static std::vector<int> sortRegionsFull(
        std::vector<NormBox>& boxes, const Params& params,
        double avg_font_height, double avg_char_width) {

        int n = (int)boxes.size();
        if (n < 2) {
            std::vector<int> result;
            for (int i = 0; i < n; ++i) result.push_back(i);
            return result;
        }

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

        std::vector<int> sorted_indices;
        sorted_indices.push_back(0);
        for (int i = 1; i < n; ++i) {
            if (boxes[i].ignore) continue;
            if (positionCompare(boxes[i], boxes[sorted_indices[0]]) < 0) {
                sorted_indices[0] = i;
            }
        }
        boxes[sorted_indices[0]].prev = 9999;

        const bool check_above_below[] = {true, true, false, true, false, true, false};
        const double gap_thresh_array[] = {-1.0, 0.0, 0.0, 0.25, 0.25, 0.5, 0.5};
        const double align_thresh_array[] = {1.0, 1.0, 0.98, 0.75, 0.75, 0.5, 0.5};

        for (int try_round = 0; try_round < 7; ++try_round) {
            double gap_thresh = gap_thresh_array[try_round];
            double align_thresh = align_thresh_array[try_round];

            if (check_above_below[try_round]) {
                align_thresh *= (1.0 - params.col_misalign_max);
            }

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

                boxes[i].next = i;

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
                    if (trappedBox(i, boxes, n, params.col_misalign_max)) {
                        boxes[i].next = -1;
                        boxes[best].prev = -1;
                    }
                }
            }
        }

        for (int i = 1; i < n; ++i) {
            if (boxes[sorted_indices[i-1]].next >= 0) {
                sorted_indices.push_back(boxes[sorted_indices[i-1]].next);
                continue;
            }

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
// 4. ParagraphParse 段落分析器
// ============================================================

class ParagraphParse {
public:
    using GetInfo = std::function<std::tuple<Nbbox, uint32_t, uint32_t>(void* handle)>;
    using SetEnd = std::function<void(void* handle, const std::string& end)>;

    ParagraphParse(GetInfo get_info, SetEnd set_end)
        : get_info_(std::move(get_info)), set_end_(std::move(set_end)) {}

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
        double bbox[4];
        uint32_t cFirst, cLast;
        void* handle;
    };
    double TH = TBPU_TH;

    std::vector<PpUnit> getUnits(const std::vector<void*>& text_blocks) {
        std::cerr << "[layout] ParagraphParse.getUnits start: text_blocks.size=" << text_blocks.size() << std::endl;
        
        std::vector<PpUnit> units;
        units.reserve(text_blocks.size());
        
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

    void parse(std::vector<PpUnit>& units) {
        std::cerr << "[layout] ParagraphParse.parse start: units.size=" << units.size() << std::endl;
        
        if (units.empty()) {
            std::cerr << "[layout] ParagraphParse.parse: units empty, return" << std::endl;
            return;
        }
        
        std::cerr << "[layout] ParagraphParse.parse: sorting units..." << std::endl;
        std::stable_sort(units.begin(), units.end(), [](const PpUnit& a, const PpUnit& b) {
            return a.bbox[1] < b.bbox[1];
        });
        std::cerr << "[layout] ParagraphParse.parse: sorted" << std::endl;

        double para_l = units[0].bbox[0];
        double para_top = units[0].bbox[1];
        double para_r = units[0].bbox[2];
        double para_bottom = units[0].bbox[3];
        double para_line_h = para_bottom - para_top;
        bool para_line_s_valid = false;
        double para_line_s = 0.0;
        std::vector<PpUnit*> now_para = {&units[0]};
        std::vector<std::vector<PpUnit*>> paras;
        std::vector<bool> paras_ls_valid;
        std::vector<double> paras_line_space;

        for (size_t i = 1; i < units.size(); ++i) {
            double l = units[i].bbox[0];
            double top = units[i].bbox[1];
            double r = units[i].bbox[2];
            double bottom = units[i].bbox[3];
            double h = bottom - top;
            double ls = top - para_bottom;
            bool same = (std::fabs(para_l - l) <= para_line_h * TH &&
                         std::fabs(para_r - r) <= para_line_h * TH &&
                         (!para_line_s_valid || ls < para_line_s + para_line_h * 0.5));
            if (same) {
                para_l = (para_l + l) / 2.0;
                para_r = (para_r + r) / 2.0;
                para_line_h = (para_line_h + h) / 2.0;
                if (!para_line_s_valid) { para_line_s = ls; para_line_s_valid = true; }
                else { para_line_s = (para_line_s + ls) / 2.0; }
                now_para.push_back(&units[i]);
            } else {
                paras.push_back(now_para);
                paras_ls_valid.push_back(para_line_s_valid);
                paras_line_space.push_back(para_line_s);
                now_para = {&units[i]};
                para_l = l;
                para_r = r;
                para_line_h = bottom - top;
                para_line_s_valid = false;
                para_line_s = 0.0;
            }
            para_bottom = bottom;
        }
        paras.push_back(now_para);
        paras_ls_valid.push_back(para_line_s_valid);
        paras_line_space.push_back(para_line_s);

        // 合并只有1行的段
        for (int i1 = (int)paras.size() - 1; i1 >= 0; --i1) {
            std::vector<PpUnit*>& para = paras[i1];
            if (para.size() != 1) continue;
            double l = para[0]->bbox[0];
            double top = para[0]->bbox[1];
            double r = para[0]->bbox[2];
            double bottom = para[0]->bbox[3];
            bool up_flag = false, down_flag = false;
            if (i1 > 0) {
                PpUnit* upUnit = paras[i1 - 1].back();
                double up_l = upUnit->bbox[0], up_top = upUnit->bbox[1];
                double up_r = upUnit->bbox[2], up_bottom = upUnit->bbox[3];
                double up_dist = std::fabs(up_l - l);
                double up_h = up_bottom - up_top;
                up_flag = (up_dist <= up_h * TH) && (r <= up_r + up_h * TH);
                if (paras_ls_valid[i1 - 1] &&
                    top - up_bottom > paras_line_space[i1 - 1] + up_h * 0.5) {
                    up_flag = false;
                }
            }
            if (i1 < (int)paras.size() - 1) {
                PpUnit* downUnit = paras[i1 + 1].front();
                double down_l = downUnit->bbox[0], down_top = downUnit->bbox[1];
                double down_r = downUnit->bbox[2], down_bottom = downUnit->bbox[3];
                double down_h = down_bottom - down_top;
                if (down_l - down_h * TH <= l && l <= down_l + down_h * (1 + TH)) {
                    if (paras[i1 + 1].size() > 1) {
                        down_flag = std::fabs(down_r - r) <= down_h * TH;
                    } else {
                        down_flag = down_r - down_h * TH < r;
                    }
                }
                if (paras_ls_valid[i1 + 1] &&
                    down_top - bottom > paras_line_space[i1 + 1] + down_h * 0.5) {
                    down_flag = false;
                }
            }
            if (up_flag && down_flag) {
                double upDist = top - (paras[i1 - 1].back()->bbox[3]);
                double downDist = (paras[i1 + 1].front()->bbox[1]) - bottom;
                if (upDist < downDist) {
                    paras[i1 - 1].push_back(para[0]);
                } else {
                    paras[i1 + 1].insert(paras[i1 + 1].begin(), para[0]);
                }
            } else if (up_flag) {
                paras[i1 - 1].push_back(para[0]);
            } else if (down_flag) {
                paras[i1 + 1].insert(paras[i1 + 1].begin(), para[0]);
            }
            if (up_flag || down_flag) {
                paras.erase(paras.begin() + i1);
                paras_ls_valid.erase(paras_ls_valid.begin() + i1);
                paras_line_space.erase(paras_line_space.begin() + i1);
            }
        }

        // 添加 end
        for (std::vector<PpUnit*>& para : paras) {
            for (size_t i1 = 0; i1 + 1 < para.size(); ++i1) {
                uint32_t letter1 = para[i1]->cLast;
                uint32_t letter2 = para[i1 + 1]->cFirst;
                std::string sep = wordSeparator(letter1, letter2);
                set_end_(para[i1]->handle, sep);
            }
            set_end_(para.back()->handle, "\n");
        }
    }
};

// ============================================================
// 5. SingleLine get_lines
// ============================================================

struct SlUnit {
    TextBlock* tb;
    Nbbox nb;
};

static std::vector<std::vector<SlUnit>> singleLineGetLines(std::vector<SlUnit> tbs) {
    std::stable_sort(tbs.begin(), tbs.end(), [](const SlUnit& a, const SlUnit& b) {
        return a.nb.x0 < b.nb.x0;
    });
    std::vector<std::vector<SlUnit>> lines;
    std::vector<char> consumed(tbs.size(), 0);
    for (size_t i1 = 0; i1 < tbs.size(); ++i1) {
        if (consumed[i1]) continue;
        double l1 = tbs[i1].nb.x0, top1 = tbs[i1].nb.y0;
        double r1 = tbs[i1].nb.x1, bottom1 = tbs[i1].nb.y1;
        double h1 = bottom1 - top1;
        std::vector<SlUnit> now_line = {tbs[i1]};
        for (size_t i2 = i1 + 1; i2 < tbs.size(); ++i2) {
            if (consumed[i2]) continue;
            double l2 = tbs[i2].nb.x0, top2 = tbs[i2].nb.y0;
            double r2 = tbs[i2].nb.x1, bottom2 = tbs[i2].nb.y1;
            double h2 = bottom2 - top2;
            if (l2 < r1 - h1) continue;
            if (top2 < top1 - h1 * 0.5 || bottom2 > bottom1 + h1 * 0.5) continue;
            if (std::fabs(h1 - h2) > std::min(h1, h2) * 0.5) continue;
            now_line.push_back(tbs[i2]);
            consumed[i2] = 1;
            r1 = r2;
        }
        for (size_t i2 = 0; i2 + 1 < now_line.size(); ++i2) {
            double l1b = now_line[i2].nb.x0, t1 = now_line[i2].nb.y0;
            double r1b = now_line[i2].nb.x1, b1 = now_line[i2].nb.y1;
            double l2 = now_line[i2 + 1].nb.x0, t2 = now_line[i2 + 1].nb.y0;
            double b2 = now_line[i2 + 1].nb.y1;
            (void)t2;
            double h = (b1 + b2 - t1 - l2) * 0.5;
            if (l2 - r1b > h * 1.5) {
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
    std::stable_sort(lines.begin(), lines.end(), [](const std::vector<SlUnit>& a, const std::vector<SlUnit>& b) {
        return a.front().nb.y0 < b.front().nb.y0;
    });
    return lines;
}

// ============================================================
// 6. 7 种排版策略
// ============================================================

static void strategySingleLine(std::vector<TextBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));
    std::vector<TextBlock> result;
    result.reserve(pp.tbs.size());
    for (std::vector<SlUnit>& line : lines) {
        for (SlUnit& u : line) {
            result.push_back(std::move(*u.tb));
        }
    }
    blocks.swap(result);
}

static void strategySinglePara(std::vector<TextBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));

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
        for (size_t i = 1; i < line.size(); ++i) {
            double bb1 = line[i].nb.y0;
            double bb2 = line[i].nb.x1;
            double bb3 = line[i].nb.y1;
            b1 = std::min(b1, bb1);
            b2 = std::max(b1, bb2);
            b3 = std::max(b1, bb3);
        }
        TempTb t;
        t.nb = {b0, b1, b2, b3};
        t.cFirst = utf8FirstCodepoint(line[0].tb->text);
        t.cLast = utf8LastCodepoint(line.back().tb->text);
        for (SlUnit& u : line) t.line.push_back(&u);
        temp_tbs.push_back(std::move(t));
    }

    auto get_info = [&temp_tbs](void* handle) -> std::tuple<Nbbox, uint32_t, uint32_t> {
        size_t idx = (size_t)(uintptr_t)handle;
        TempTb& t = temp_tbs[idx];
        return std::make_tuple(t.nb, t.cFirst, t.cLast);
    };
    auto set_end = [&temp_tbs](void* handle, const std::string& end) {
        size_t idx = (size_t)(uintptr_t)handle;
        temp_tbs[idx].line.back()->tb->end = end;
    };
    ParagraphParse pp_parser(get_info, set_end);
    std::vector<void*> handles;
    handles.reserve(temp_tbs.size());
    for (size_t i = 0; i < temp_tbs.size(); ++i) handles.push_back((void*)(uintptr_t)i);
    pp_parser.run(handles);

    std::vector<TextBlock> result;
    result.reserve(pp.tbs.size());
    for (TempTb& t : temp_tbs) {
        for (SlUnit* u : t.line) {
            result.push_back(std::move(*u->tb));
        }
    }
    blocks.swap(result);
}

static void strategySingleNone(std::vector<TextBlock>& blocks) {
    strategySingleLine(blocks);
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        if (blocks[i].end == "\n") {
            uint32_t letter1 = utf8LastCodepoint(blocks[i].text);
            uint32_t letter2 = utf8FirstCodepoint(blocks[i + 1].text);
            blocks[i].end = wordSeparator(letter1, letter2);
        }
    }
}

static TextBlock singleCodeMergeLine(std::vector<SlUnit>& line) {
    TextBlock A = *line[0].tb;
    double ba[8];
    for (int k = 0; k < 4; ++k) {
        ba[k * 2] = (double)A.boxPoint[k].x;
        ba[k * 2 + 1] = (double)A.boxPoint[k].y;
    }
    double ha = ba[5] - ba[1];
    float score = A.boxScore;
    for (size_t i = 1; i < line.size(); ++i) {
        TextBlock& B = *line[i].tb;
        double bb[8];
        for (int k = 0; k < 4; ++k) {
            bb[k * 2] = (double)B.boxPoint[k].x;
            bb[k * 2 + 1] = (double)B.boxPoint[k].y;
        }
        ha = (ha + (bb[5] - bb[1])) / 2.0;
        int space = 0;
        if (bb[0] > ba[2]) {
            space = (int)std::lround((bb[0] - ba[2]) / ha);
        }
        if (space < 0) space = 0;
        A.text += std::string((size_t)space * 2, ' ') + B.text;
        double yTop = std::min({ba[1], ba[3], bb[1], bb[3]});
        double yBottom = std::max({ba[5], ba[7], bb[5], bb[7]});
        double xLeft = std::min({ba[0], ba[6], bb[0], bb[6]});
        double xRight = std::max({ba[2], ba[4], bb[2], bb[4]});
        ba[1] = yTop;  ba[3] = yTop;
        ba[5] = yBottom; ba[7] = yBottom;
        ba[0] = xLeft; ba[6] = xLeft;
        ba[2] = xRight; ba[4] = xRight;
        score += B.boxScore;
    }
    A.boxScore = score / (float)line.size();
    A.end = "\n";
    A.boxPoint[0] = cv::Point((int)ba[0], (int)ba[1]);
    A.boxPoint[1] = cv::Point((int)ba[2], (int)ba[3]);
    A.boxPoint[2] = cv::Point((int)ba[4], (int)ba[5]);
    A.boxPoint[3] = cv::Point((int)ba[6], (int)ba[7]);
    return A;
}

static size_t bisectLeft(const std::vector<double>& a, double x) {
    size_t lo = 0, hi = a.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] < x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void singleCodeIndent(std::vector<TextBlock>& tbs) {
    if (tbs.empty()) return;
    double lh = 0;
    double xMin = std::numeric_limits<double>::infinity();
    double xMax = -std::numeric_limits<double>::infinity();
    for (TextBlock& tb : tbs) {
        lh += (double)tb.boxPoint[3].y - (double)tb.boxPoint[0].y;
        double x = (double)tb.boxPoint[0].x;
        if (x < xMin) xMin = x;
        if (x > xMax) xMax = x;
    }
    lh /= (double)tbs.size();
    double lh2 = lh / 2.0;
    std::vector<double> levelList;
    double x = xMin;
    while (x < xMax) {
        levelList.push_back(x);
        x += lh;
    }
    for (TextBlock& tb : tbs) {
        size_t pos = bisectLeft(levelList, (double)tb.boxPoint[0].x + lh2);
        long level = (long)pos - 1;
        if (level < 0) level = 0;
        tb.text = std::string((size_t)level * 2, ' ') + tb.text;
        tb.boxPoint[0].x = (int)xMin;
        tb.boxPoint[3].x = (int)xMin;
    }
}

static void strategySingleCode(std::vector<TextBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::vector<SlUnit> tbs;
    tbs.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) {
        tbs.push_back({pp.tbs[i], pp.nbboxes[i]});
    }
    std::vector<std::vector<SlUnit>> lines = singleLineGetLines(std::move(tbs));
    std::vector<TextBlock> tbsOut;
    tbsOut.reserve(lines.size());
    for (std::vector<SlUnit>& line : lines) {
        tbsOut.push_back(singleCodeMergeLine(line));
    }
    singleCodeIndent(tbsOut);
    blocks.swap(tbsOut);
}


// ---- single_vertical ----
static void strategySingleVertical(std::vector<TextBlock>& blocks) {
    if (blocks.empty()) return;
    double pageW = 0, pageH = 0;
    for (TextBlock& b : blocks) {
        for (int i = 0; i < 4; ++i) {
            if (b.boxPoint[i].x > pageW) pageW = b.boxPoint[i].x;
            if (b.boxPoint[i].y > pageH) pageH = b.boxPoint[i].y;
        }
    }
    double cx = pageW / 2.0, cy = pageH / 2.0;
    for (TextBlock& b : blocks) {
        for (int i = 0; i < 4; ++i) {
            double bx = (double)b.boxPoint[i].x;
            double by = (double)b.boxPoint[i].y;
            b.boxPoint[i].x = (int)std::lround(by + cx - cy);
            b.boxPoint[i].y = (int)std::lround(cx + cy - bx);
        }
    }
    strategySingleLine(blocks);
}

static void strategyMultiLine(std::vector<TextBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::unordered_map<TextBlock*, Nbbox> nbMap;
    nbMap.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) nbMap[pp.tbs[i]] = pp.nbboxes[i];
    GapTree gtree([&nbMap](TextBlock* tb) -> Nbbox { return nbMap.at(tb); });

    std::vector<TextBlock*> sorted = gtree.sort(pp.tbs);
    std::vector<std::vector<TextBlock*>> nodes = gtree.getNodesTextBlocks();

    FineSort::Params fine_params;
    std::vector<TextBlock*> final_sorted;
    for (std::vector<TextBlock*>& region_blocks : nodes) {
        if (region_blocks.size() > 1) {
            std::vector<TextBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
            final_sorted.insert(final_sorted.end(), region_sorted.begin(), region_sorted.end());
        } else {
            final_sorted.insert(final_sorted.end(), region_blocks.begin(), region_blocks.end());
        }
    }

    std::vector<TextBlock> result;
    result.reserve(final_sorted.size());
    for (TextBlock* tb : final_sorted) {
        tb->end = "\n";
        result.push_back(std::move(*tb));
    }
    blocks.swap(result);
}

static void strategyMultiPara(std::vector<TextBlock>& blocks) {
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
        
        std::unordered_map<TextBlock*, Nbbox> nbMap;
        nbMap.reserve(pp.tbs.size());
        for (size_t i = 0; i < pp.tbs.size(); ++i) {
            nbMap[pp.tbs[i]] = pp.nbboxes[i];
        }
        std::cerr << "[layout] nbMap created: size=" << nbMap.size() << std::endl;
        
        GapTree gtree([&nbMap](TextBlock* tb) -> Nbbox { 
            try {
                return nbMap.at(tb);
            } catch (const std::exception& e) {
                std::cerr << "[layout] ERROR in GapTree get_bbox: " << e.what() << std::endl;
                throw;
            }
        });
        std::cerr << "[layout] GapTree created" << std::endl;
        
        std::cerr << "[layout] calling GapTree.sort..." << std::endl;
        std::vector<TextBlock*> sorted = gtree.sort(pp.tbs);
        std::cerr << "[layout] GapTree.sort done: sorted.size=" << sorted.size() << std::endl;
        
        std::cerr << "[layout] calling GapTree.getNodesTextBlocks..." << std::endl;
        std::vector<std::vector<TextBlock*>> nodes = gtree.getNodesTextBlocks();
        std::cerr << "[layout] GapTree.getNodesTextBlocks done: nodes.size=" << nodes.size() << std::endl;
        
        FineSort::Params fine_params;
        std::vector<std::vector<TextBlock*>> sorted_nodes;
        std::cerr << "[layout] processing nodes with FineSort..." << std::endl;
        
        for (size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
            std::vector<TextBlock*>& region_blocks = nodes[nodeIdx];
            std::cerr << "[layout] node[" << nodeIdx << "]: region_blocks.size=" << region_blocks.size() << std::endl;
            
            if (region_blocks.size() > 1) {
                std::cerr << "[layout] calling FineSort.sortRegion for node[" << nodeIdx << "]..." << std::endl;
                try {
                    std::vector<TextBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
                    std::cerr << "[layout] FineSort.sortRegion done: region_sorted.size=" << region_sorted.size() << std::endl;
                    sorted_nodes.push_back(std::move(region_sorted));
                } catch (const std::exception& e) {
                    std::cerr << "[layout] FineSort.sortRegion FAILED for node[" << nodeIdx << "]: " << e.what() << std::endl;
                    sorted_nodes.push_back(std::move(region_blocks));
                }
            } else {
                std::cerr << "[layout] node[" << nodeIdx << "] has only 1 block, skip FineSort" << std::endl;
                sorted_nodes.push_back(std::move(region_blocks));
            }
        }
        std::cerr << "[layout] FineSort processing done: sorted_nodes.size=" << sorted_nodes.size() << std::endl;

        auto get_info = [&nbMap](void* handle) -> std::tuple<Nbbox, uint32_t, uint32_t> {
            try {
                std::cerr << "[layout] get_info lambda: handle=" << handle << std::endl;
                
                TextBlock* tb = static_cast<TextBlock*>(handle);
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
                
                TextBlock* tb = static_cast<TextBlock*>(handle);
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

        std::cerr << "[layout] running ParagraphParse for each node..." << std::endl;
        for (size_t nodeIdx = 0; nodeIdx < sorted_nodes.size(); ++nodeIdx) {
            std::vector<TextBlock*>& tbs = sorted_nodes[nodeIdx];
            std::cerr << "[layout] ParagraphParse node[" << nodeIdx << "]: tbs.size=" << tbs.size() << std::endl;
            
            std::vector<void*> handles;
            handles.reserve(tbs.size());
            for (TextBlock* tb : tbs) handles.push_back(tb);
            
            try {
                pp_parser.run(handles);
                std::cerr << "[layout] ParagraphParse node[" << nodeIdx << "] done" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[layout] ParagraphParse FAILED for node[" << nodeIdx << "]: " << e.what() << std::endl;
                for (TextBlock* tb : tbs) {
                    tb->end = "\n";
                }
            }
        }
        
        std::cerr << "[layout] building result..." << std::endl;
        std::vector<TextBlock> result;
        for (std::vector<TextBlock*>& tbs : sorted_nodes) {
            for (TextBlock* tb : tbs) {
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

static void strategyMultiNone(std::vector<TextBlock>& blocks) {
    Preprocessed pp = linePreprocessing(blocks);
    if (pp.tbs.empty()) { blocks.clear(); return; }
    std::unordered_map<TextBlock*, Nbbox> nbMap;
    nbMap.reserve(pp.tbs.size());
    for (size_t i = 0; i < pp.tbs.size(); ++i) nbMap[pp.tbs[i]] = pp.nbboxes[i];
    GapTree gtree([&nbMap](TextBlock* tb) -> Nbbox { return nbMap.at(tb); });

    std::vector<TextBlock*> sorted = gtree.sort(pp.tbs);
    std::vector<std::vector<TextBlock*>> nodes = gtree.getNodesTextBlocks();

    FineSort::Params fine_params;
    std::vector<TextBlock*> final_sorted;
    for (std::vector<TextBlock*>& region_blocks : nodes) {
        if (region_blocks.size() > 1) {
            std::vector<TextBlock*> region_sorted = FineSort::sortRegion(region_blocks, nbMap, fine_params);
            final_sorted.insert(final_sorted.end(), region_sorted.begin(), region_sorted.end());
        } else {
            final_sorted.insert(final_sorted.end(), region_blocks.begin(), region_blocks.end());
        }
    }

    std::vector<TextBlock> result;
    result.reserve(final_sorted.size());
    for (size_t i = 0; i < final_sorted.size(); ++i) {
        TextBlock* tb = final_sorted[i];
        if (i < final_sorted.size() - 1) {
            TextBlock* next = final_sorted[i + 1];
            tb->end = wordSeparatorText(tb->text, next->text);
        } else {
            tb->end = "\n";
        }
        result.push_back(std::move(*tb));
    }
    blocks.swap(result);
}

// ============================================================
// 对外接口
// ============================================================

void applyLayout(std::vector<TextBlock>& blocks, const std::string& strategyKey) {
    try {
        std::cerr << "[layout] applyLayout start: blocks=" << blocks.size() 
                  << ", strategy=" << strategyKey << std::endl;
        
        if (blocks.empty()) {
            std::cerr << "[layout] blocks empty, skip" << std::endl;
            return;
        }
        
        for (size_t i = 0; i < blocks.size(); ++i) {
            const TextBlock& b = blocks[i];
            std::cerr << "[layout] block[" << i << "]: text='" << b.text 
                      << "', boxPoint.size=4"
                      << ", boxScore=" << b.boxScore << std::endl;
            
            for (int j = 0; j < 4; ++j) {
                if (b.boxPoint[j].x < 0 || b.boxPoint[j].x > 10000 ||
                    b.boxPoint[j].y < 0 || b.boxPoint[j].y > 10000) {
                    std::cerr << "[layout] WARNING: block[" << i << "] boxPoint[" << j 
                              << "] has unusual value: (" << b.boxPoint[j].x << ", " << b.boxPoint[j].y << ")" << std::endl;
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
        throw;
    } catch (...) {
        std::cerr << "[layout] UNKNOWN EXCEPTION in applyLayout" << std::endl;
        std::cerr << "[layout] strategy=" << strategyKey << ", blocks=" << blocks.size() << std::endl;
        throw;
    }
}
