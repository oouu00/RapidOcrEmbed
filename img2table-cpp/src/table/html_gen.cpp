#include "table/html_gen.h"
#include <cmath>

namespace table {

std::string generateHTML(const Table& table) {
    std::string html = "<table>\n";
    for (auto& row : table.rows) {
        html += "  <tr>\n";
        size_t i = 0;
        while (i < row.cells.size()) {
            auto& cell = row.cells[i];
            int colspan = 1;
            while (i + colspan < row.cells.size()) {
                auto& next = row.cells[i + colspan];
                if (next.x1 == cell.x1 && next.y1 == cell.y1 &&
                    next.x2 == cell.x2 && next.y2 == cell.y2) {
                    colspan++;
                } else {
                    break;
                }
            }
            html += "    <td";
            if (colspan > 1) {
                html += " colspan=\"" + std::to_string(colspan) + "\"";
            }
            if (!cell.content.empty()) {
                html += ">" + cell.content + "</td>\n";
            } else {
                html += ">&nbsp;</td>\n";
            }
            i += colspan;
        }
        html += "  </tr>\n";
    }
    html += "</table>\n";
    return html;
}

} // namespace table
