#ifndef HTMLTOXLSX_H
#define HTMLTOXLSX_H

// 防止 Windows max/min 宏与 std::max/min 冲突
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include "xlsxwriter.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace HtmlToXlsx {

struct RawCell {
    std::string text;
    int colspan = 1;
    int rowspan = 1;
    int width_px = 0;
    int height_px = 0;
    double width_pct = 0.0;
    double height_pct = 0.0;
};

struct GridCell {
    std::string text;
    int colspan = 1;
    int rowspan = 1;
};

static std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

static std::string extractAttr(const std::string& tag, const std::string& name) {
    std::string lowerTag = toLowerStr(tag);
    std::string lowerName = toLowerStr(name);
    size_t pos = lowerTag.find(lowerName);
    if (pos == std::string::npos) return "";
    pos += lowerName.length();
    while (pos < lowerTag.size() && (lowerTag[pos] == ' ' || lowerTag[pos] == '=')) pos++;
    if (pos >= lowerTag.size()) return "";
    char quote = lowerTag[pos];
    if (quote != '\'' && quote != '"') {
        size_t end = lowerTag.find_first_of(" \t>", pos);
        if (end == std::string::npos) end = lowerTag.size();
        return tag.substr(pos, end - pos);
    }
    pos++;
    size_t end = tag.find(quote, pos);
    if (end == std::string::npos) end = tag.size();
    return tag.substr(pos, end - pos);
}

static int getIntAttr(const std::string& tag, const std::string& name, int def = 0) {
    std::string v = extractAttr(tag, name);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

static int getStylePx(const std::string& style, const std::string& prop) {
    size_t pos = 0;
    std::string lowerStyle = toLowerStr(style);
    std::string lowerProp = toLowerStr(prop);
    while (pos < lowerStyle.size()) {
        pos = lowerStyle.find(lowerProp, pos);
        if (pos == std::string::npos) return 0;
        pos += lowerProp.size();
        while (pos < lowerStyle.size() && lowerStyle[pos] == ' ') pos++;
        if (pos < lowerStyle.size() && lowerStyle[pos] == ':') {
            pos++;
            while (pos < lowerStyle.size() && lowerStyle[pos] == ' ') pos++;
            size_t start = pos;
            while (pos < lowerStyle.size() && lowerStyle[pos] >= '0' && lowerStyle[pos] <= '9') pos++;
            if (pos > start) {
                return std::atoi(style.substr(start, pos - start).c_str());
            }
        }
    }
    return 0;
}

static double getStylePct(const std::string& style, const std::string& prop) {
    size_t pos = 0;
    std::string lowerStyle = toLowerStr(style);
    std::string lowerProp = toLowerStr(prop);
    while (pos < lowerStyle.size()) {
        pos = lowerStyle.find(lowerProp, pos);
        if (pos == std::string::npos) return 0.0;
        pos += lowerProp.size();
        while (pos < lowerStyle.size() && lowerStyle[pos] == ' ') pos++;
        if (pos < lowerStyle.size() && lowerStyle[pos] == ':') {
            pos++;
            while (pos < lowerStyle.size() && lowerStyle[pos] == ' ') pos++;
            size_t start = pos;
            while (pos < lowerStyle.size() &&
                   ((lowerStyle[pos] >= '0' && lowerStyle[pos] <= '9') || lowerStyle[pos] == '.'))
                pos++;
            if (pos > start) {
                double val = std::atof(style.substr(start, pos - start).c_str());
                if (pos < lowerStyle.size() && lowerStyle[pos] == '%') return val;
                return 0.0;
            }
        }
    }
    return 0.0;
}

static std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string decodeHtmlEntities(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.compare(i, 6, "&nbsp;") == 0)      { r += "\xC2\xA0"; i += 5; }
            else if (s.compare(i, 5, "&amp;") == 0)   { r += '&';    i += 4; }
            else if (s.compare(i, 4, "&lt;") == 0)    { r += '<';    i += 3; }
            else if (s.compare(i, 4, "&gt;") == 0)    { r += '>';    i += 3; }
            else if (s.compare(i, 6, "&quot;") == 0)   { r += '"';    i += 5; }
            else if (s.compare(i, 6, "&apos;") == 0)   { r += '\'';   i += 5; }
            else if (s.compare(i, 5, "&#39;") == 0)    { r += '\'';   i += 4; }
            else if (s.compare(i, 3, "&#x") == 0 || s.compare(i, 2, "&#") == 0) {
                // numeric entity: &#NNN; or &#xHHH;
                size_t semi = s.find(';', i);
                if (semi != std::string::npos) {
                    std::string entity = s.substr(i + 2, semi - i - 2);
                    int code = 0;
                    if (entity[0] == 'x' || entity[0] == 'X')
                        code = std::strtol(entity.c_str() + 1, nullptr, 16);
                    else
                        code = std::strtol(entity.c_str(), nullptr, 10);
                    if (code > 0 && code < 128)
                        r += (char)code;
                    i = semi;
                } else {
                    r += s[i];
                }
            }
            else { r += s[i]; }
        } else {
            r += s[i];
        }
    }
    return r;
}

static std::string findTable(const std::string& html) {
    std::string lower = toLowerStr(html);
    size_t s = lower.find("<table");
    if (s == std::string::npos) return "";
    size_t e = lower.find("</table>", s);
    if (e == std::string::npos) return "";
    return html.substr(s, e - s + 8);
}

static std::vector<std::pair<std::string, std::string>> splitByTagPairs(
    const std::string& html, const std::string& tag)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::string lowerHtml = toLowerStr(html);
    std::string openTag = "<" + tag;
    std::string closeTag = "</" + tag + ">";

    size_t pos = 0;
    while (pos < html.size()) {
        size_t tagStart = lowerHtml.find(openTag, pos);
        if (tagStart == std::string::npos) break;
        size_t tagEnd = html.find('>', tagStart);
        if (tagEnd == std::string::npos) break;
        std::string openingTag = html.substr(tagStart, tagEnd - tagStart + 1);
        size_t contentStart = tagEnd + 1;
        size_t contentEnd = lowerHtml.find(closeTag, contentStart);
        if (contentEnd == std::string::npos) contentEnd = html.size();
        std::string content = html.substr(contentStart, contentEnd - contentStart);
        result.push_back({openingTag, content});
        pos = contentEnd + closeTag.size();
    }
    return result;
}

struct ParseResult {
    std::vector<std::vector<RawCell>> rows;
    std::vector<double> colWidthPct;   // from <col style='width:X%'>
    std::vector<double> rowHeightPct;  // from <tr style='height:X%'>
};

static ParseResult parseHtmlRaw(const std::string& html) {
    ParseResult result;
    std::string table = findTable(html);
    if (table.empty()) return result;

    // Parse <colgroup> for column width percentages
    std::string lowerTable = toLowerStr(table);
    size_t cgStart = lowerTable.find("<colgroup");
    if (cgStart != std::string::npos) {
        size_t cgEnd = lowerTable.find("</colgroup>", cgStart);
        if (cgEnd != std::string::npos) {
            std::string colgroup = table.substr(cgStart, cgEnd - cgStart);
            size_t cpos = 0;
            while (cpos < colgroup.size()) {
                size_t colTag = colgroup.find("<col", cpos);
                if (colTag == std::string::npos) break;
                size_t colEnd = colgroup.find('>', colTag);
                if (colEnd == std::string::npos) break;
                std::string colStr = colgroup.substr(colTag, colEnd - colTag + 1);
                std::string colStyle = extractAttr(colStr, "style");
                double pct = getStylePct(colStyle, "width");
                if (pct > 0) result.colWidthPct.push_back(pct);
                cpos = colEnd + 1;
            }
        }
    }

    auto trPairs = splitByTagPairs(table, "tr");
    for (auto& trPair : trPairs) {
        std::vector<RawCell> rowCells;
        // Parse row height percentage from <tr style='height:X%'>
        double rowPct = getStylePct(trPair.first, "height");
        auto tdPairs = splitByTagPairs(trPair.second, "td");
        for (auto& tdPair : tdPairs) {
            RawCell cell;
            std::string& tag = tdPair.first;
            cell.colspan = getIntAttr(tag, "colspan", 1);
            cell.rowspan = getIntAttr(tag, "rowspan", 1);
            std::string style = extractAttr(tag, "style");
            cell.width_px = getStylePx(style, "width");
            cell.height_px = getStylePx(style, "height");
            cell.width_pct = getStylePct(style, "width");
            cell.height_pct = getStylePct(style, "height");
            if (rowPct > 0) cell.height_pct = rowPct;
            cell.text = decodeHtmlEntities(trimStr(tdPair.second));
            rowCells.push_back(cell);
        }
        if (!rowCells.empty()) {
            result.rows.push_back(rowCells);
            result.rowHeightPct.push_back(rowPct);
        }
    }
    return result;
}

static void buildGrid(const std::vector<std::vector<RawCell>>& rawRows,
                       std::vector<std::vector<GridCell>>& grid,
                       int& maxCol) {
    int numRows = (int)rawRows.size();
    maxCol = 0;
    if (numRows == 0) return;

    int gridCols = numRows * 8;
    std::vector<std::vector<int>> occ(numRows, std::vector<int>(gridCols, -1));
    grid.assign(numRows, std::vector<GridCell>(gridCols));

    for (int r = 0; r < numRows; ++r) {
        int ci = 0;
        for (int col = 0; col < gridCols && ci < (int)rawRows[r].size(); ++col) {
            if (occ[r][col] != -1) continue;
            const RawCell& raw = rawRows[r][ci];
            int cs = raw.colspan;
            int rs = raw.rowspan;
            grid[r][col].text = raw.text;
            grid[r][col].colspan = cs;
            grid[r][col].rowspan = rs;
            for (int dr = 0; dr < rs && r + dr < numRows; ++dr)
                for (int dc = 0; dc < cs && col + dc < gridCols; ++dc)
                    occ[r + dr][col + dc] = r;
            ci++;
        }
    }

    for (int r = 0; r < numRows; ++r) {
        int c = 0;
        for (int col = 0; col < gridCols; ++col)
            if (occ[r][col] != -1) c = col + 1;
        if (c > maxCol) maxCol = c;
    }
    for (int r = 0; r < numRows; ++r)
        grid[r].resize(maxCol);
}

} // namespace HtmlToXlsx

static bool htmlToXlsx(const std::string& html, const std::string& xlsxPath) {
    using namespace HtmlToXlsx;

    ParseResult pr = parseHtmlRaw(html);
    if (pr.rows.empty()) return false;

    auto& rawRows = pr.rows;
    std::vector<std::vector<GridCell>> grid;
    int maxCol = 0;
    buildGrid(rawRows, grid, maxCol);
    if (maxCol == 0) return false;

    int numRows = (int)grid.size();

    // ── 判断是否有百分比信息（新版HTML） ──
    bool hasPctCols = !pr.colWidthPct.empty() && (int)pr.colWidthPct.size() == maxCol;
    bool hasPctRows = false;
    for (double p : pr.rowHeightPct) if (p > 0) { hasPctRows = true; break; }

    // ── 旧版：收集像素宽高 ──
    std::vector<int> colPx(maxCol, 0);
    std::vector<int> rowPx(numRows, 0);
    if (!hasPctCols) {
        for (int r = 0; r < numRows; ++r) {
            int col = 0;
            for (auto& raw : rawRows[r]) {
                if (raw.width_px > 0) {
                    int perCol = raw.width_px / raw.colspan;
                    for (int k = 0; k < raw.colspan && col + k < maxCol; ++k)
                        colPx[col + k] = std::max(colPx[col + k], perCol);
                }
                col += raw.colspan;
            }
        }
    }
    if (!hasPctRows) {
        for (int r = 0; r < numRows; ++r)
            for (auto& raw : rawRows[r])
                if (raw.height_px > rowPx[r]) rowPx[r] = raw.height_px;
    }

    // ── 解析 aspect-ratio（旧版 fallback） ──
    auto parseAspect = [&](double& wh) {
        size_t arPos = html.find("aspect-ratio");
        if (arPos == std::string::npos) return;
        size_t colon = html.find(':', arPos);
        if (colon == std::string::npos) return;
        size_t start = html.find_first_not_of(" \t", colon + 1);
        size_t slash = html.find('/', start);
        if (slash == std::string::npos) return;
        std::string wStr = html.substr(start, slash - start);
        size_t end = html.find_first_of(" ;)", slash + 1);
        std::string hStr = html.substr(slash + 1, end - slash - 1);
        try {
            double w = std::stod(wStr);
            double h = std::stod(hStr);
            if (w > 0 && h > 0) wh = h / w;
        } catch (...) {}
    };

    // No explicit width: default 88px per column
    if (!hasPctCols) {
        bool hasW = false;
        for (int c = 0; c < maxCol; ++c) if (colPx[c] > 0) { hasW = true; break; }
        if (!hasW)
            for (int c = 0; c < maxCol; ++c) colPx[c] = 88;
    }

    // No explicit height: parse aspect-ratio from CSS
    if (!hasPctRows) {
        bool hasH = false;
        for (int r = 0; r < numRows; ++r) if (rowPx[r] > 0) { hasH = true; break; }
        if (!hasH) {
            double aspectWH = 1.35;
            parseAspect(aspectWH);
            int totalPxW = 0;
            for (int c = 0; c < maxCol; ++c) totalPxW += colPx[c];
            int totalPxH = (int)(totalPxW * aspectWH);
            int h = totalPxH / numRows;
            if (h < 30) h = 30;
            for (int r = 0; r < numRows; ++r) rowPx[r] = h;
        }
    }

    // ── 计算列宽（字符数） ──
    std::vector<double> colWidths(maxCol, 0);
    if (hasPctCols) {
        // 新版：百分比 → 字符数（总宽80字符，按比例分配）
        for (int c = 0; c < maxCol; ++c)
            colWidths[c] = pr.colWidthPct[c] / 100.0 * 80.0;
    } else {
        // 旧版：像素 → 字符数
        double totalPxW = 0;
        for (int c = 0; c < maxCol; ++c) totalPxW += colPx[c];
        if (totalPxW <= 0) totalPxW = maxCol * 80;
        for (int c = 0; c < maxCol; ++c)
            colWidths[c] = colPx[c] / totalPxW * 80.0;
    }

    // ── 计算行高（磅） ──
    std::vector<double> rowHeights(numRows, 28.0);
    if (hasPctRows) {
        // 新版：百分比 → 磅（总高 = 行数 × 28，按比例分配）
        double totalPt = numRows * 28.0;
        for (int r = 0; r < numRows; ++r) {
            if (r < (int)pr.rowHeightPct.size() && pr.rowHeightPct[r] > 0)
                rowHeights[r] = pr.rowHeightPct[r] / 100.0 * totalPt;
        }
    } else {
        // 旧版：像素 → 磅
        double totalPxH = 0;
        for (int r = 0; r < numRows; ++r) totalPxH += rowPx[r];
        if (totalPxH <= 0) totalPxH = numRows * 30;
        double S = 16.0;
        double minRowPx = 999;
        for (int r = 0; r < numRows; ++r) if (rowPx[r] > 0 && rowPx[r] < minRowPx) minRowPx = rowPx[r];
        if (minRowPx > 0) S = 16.0 / minRowPx;
        for (int r = 0; r < numRows; ++r)
            rowHeights[r] = rowPx[r] * S;
    }

    // ── 写入 XLSX ──
    lxw_workbook* workbook = workbook_new(xlsxPath.c_str());
    if (!workbook) return false;
    lxw_worksheet* worksheet = workbook_add_worksheet(workbook, NULL);

    lxw_format* border_fmt = workbook_add_format(workbook);
    format_set_border(border_fmt, LXW_BORDER_THIN);
    format_set_align(border_fmt, LXW_ALIGN_VERTICAL_CENTER);
    format_set_text_wrap(border_fmt);

    lxw_format* border_empty = workbook_add_format(workbook);
    format_set_border(border_empty, LXW_BORDER_THIN);

    for (int c = 0; c < maxCol; ++c)
        worksheet_set_column(worksheet, c, c, colWidths[c], NULL);

    std::vector<std::vector<bool>> written(numRows, std::vector<bool>(maxCol, false));

    for (int r = 0; r < numRows; ++r) {
        worksheet_set_row(worksheet, r, rowHeights[r], NULL);

        for (int c = 0; c < maxCol; ++c) {
            if (written[r][c]) continue;
            int cs = grid[r][c].colspan;
            int rs = grid[r][c].rowspan;
            if (cs > 1 || rs > 1) {
                int lastRow = r + rs - 1;
                int lastCol = c + cs - 1;
                for (int dr = 0; dr < rs; ++dr) {
                    for (int dc = 0; dc < cs; ++dc) {
                        int rr = r + dr, cc = c + dc;
                        if (dr == 0 && dc == 0)
                            worksheet_write_string(worksheet, rr, cc, grid[r][c].text.c_str(), border_fmt);
                        else
                            worksheet_write_string(worksheet, rr, cc, "", border_empty);
                        if (rr < numRows && cc < maxCol) written[rr][cc] = true;
                    }
                }
                worksheet_merge_range(worksheet, r, c, lastRow, lastCol, NULL, NULL);
            } else {
                worksheet_write_string(worksheet, r, c, grid[r][c].text.c_str(), border_fmt);
                written[r][c] = true;
            }
        }
    }

    workbook_close(workbook);
    return true;
}

#endif // HTMLTOXLSX_H
