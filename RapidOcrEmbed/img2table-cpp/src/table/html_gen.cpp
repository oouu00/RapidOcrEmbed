#include "table/html_gen.h"
#include <cmath>
#include <set>

namespace table {

std::string generateHTML(const Table& table) {
    if (table.rows.empty()) return "";

    size_t maxCols = 0;
    for (auto& r : table.rows)
        maxCols = std::max(maxCols, r.cells.size());

    std::vector<std::vector<bool>> skip(table.rows.size());
    for (auto& s : skip) s.assign(maxCols, false);

    // Collect all unique x coordinates to determine column widths
    std::set<int32_t> xvals;
    for (auto& r : table.rows)
        for (auto& c : r.cells) {
            xvals.insert(c.x1);
            xvals.insert(c.x2);
        }
    std::vector<int32_t> xsorted(xvals.begin(), xvals.end());

    // Compute total table dimensions
    int32_t tableW = table.x2() - table.x1();
    int32_t tableH = table.y2() - table.y1();
    if (tableW <= 0) tableW = 1;
    if (tableH <= 0) tableH = 1;

    std::string html = "<table style='table-layout:fixed;width:100%'>\n";
    // Column widths as percentage
    if (xsorted.size() > 1) {
        html += "  <colgroup>\n";
        for (size_t i = 0; i + 1 < xsorted.size(); i++) {
            double pct = static_cast<double>(xsorted[i + 1] - xsorted[i]) / tableW * 100.0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", pct);
            html += "    <col style='width:" + std::string(buf) + "%'>\n";
        }
        html += "  </colgroup>\n";
    }

    for (size_t ri = 0; ri < table.rows.size(); ri++) {
        auto& row = table.rows[ri];
        double hPct = static_cast<double>(row.y2() - row.y1()) / tableH * 100.0;
        char hBuf[32];
        snprintf(hBuf, sizeof(hBuf), "%.2f", hPct);
        html += "  <tr style='height:" + std::string(hBuf) + "%'>\n";
        for (size_t ci = 0; ci < row.cells.size(); ci++) {
            if (skip[ri][ci]) continue;

            auto& cell = row.cells[ci];

            int colspan = 1;
            while (ci + colspan < row.cells.size()) {
                auto& next = row.cells[ci + colspan];
                if (next.x1 == cell.x1 && next.y1 == cell.y1 &&
                    next.x2 == cell.x2 && next.y2 == cell.y2) {
                    colspan++;
                } else {
                    break;
                }
            }

            int rowspan = 1;
            for (size_t rj = ri + 1; rj < table.rows.size(); rj++) {
                bool allMatch = true;
                for (int k = 0; k < colspan; k++) {
                    size_t idx = ci + k;
                    if (idx >= table.rows[rj].cells.size() || skip[rj][idx]) {
                        allMatch = false;
                        break;
                    }
                    auto& candidate = table.rows[rj].cells[idx];
                    if (candidate.x1 != cell.x1 || candidate.y1 != cell.y1 ||
                        candidate.x2 != cell.x2 || candidate.y2 != cell.y2) {
                        allMatch = false;
                        break;
                    }
                }
                if (!allMatch) break;
                rowspan++;
                for (int k = 0; k < colspan; k++)
                    skip[rj][ci + k] = true;
            }

            // Mark cells in current row that are merged by colspan
            for (int k = 1; k < colspan; k++)
                skip[ri][ci + k] = true;

            html += "    <td";
            if (rowspan > 1)
                html += " rowspan=\"" + std::to_string(rowspan) + "\"";
            if (colspan > 1)
                html += " colspan=\"" + std::to_string(colspan) + "\"";
            if (!cell.content.empty())
                html += ">" + cell.content + "</td>\n";
            else
                html += ">&nbsp;</td>\n";
        }
        html += "  </tr>\n";
    }
    html += "</table>\n";
    return html;
}

} // namespace table
