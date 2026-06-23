#include "table/creation.h"
#include <algorithm>
#include <set>
#include <cmath>
#include <climits>

namespace table {

Table clusterToTable(const std::vector<Cell>& clusterCells,
                     const std::vector<Cell>& elements) {
    Table table;
    if (clusterCells.empty()) return table;

    std::set<int32_t> vSet, hSet;
    for (auto& c : clusterCells) {
        vSet.insert(c.y1); vSet.insert(c.y2);
        hSet.insert(c.x1); hSet.insert(c.x2);
    }
    std::vector<int32_t> vDelims(vSet.begin(), vSet.end());
    std::vector<int32_t> hDelims(hSet.begin(), hSet.end());
    std::sort(vDelims.begin(), vDelims.end());
    std::sort(hDelims.begin(), hDelims.end());

    for (size_t ri = 0; ri + 1 < vDelims.size(); ri++) {
        int32_t yTop = vDelims[ri], yBot = vDelims[ri + 1];
        if (yBot - yTop <= 0) continue;

        std::vector<Cell> matchingCells;
        for (auto& c : clusterCells) {
            int64_t overlap = static_cast<int64_t>(std::min(c.y2, yBot)) - std::max(c.y1, yTop);
            if (overlap >= 0.9 * (yBot - yTop)) {
                matchingCells.push_back(c);
            }
        }

        Row row;
        for (size_t ci = 0; ci + 1 < hDelims.size(); ci++) {
            int32_t xLeft = hDelims[ci], xRight = hDelims[ci + 1];
            if (xRight - xLeft <= 0) continue;

            Cell defaultCell(xLeft, yTop, xRight, yBot);

            const Cell* containingCell = nullptr;
            for (auto& mc : matchingCells) {
                int64_t xo = static_cast<int64_t>(std::max(0, std::min(defaultCell.x2, mc.x2) -
                                                              std::max(defaultCell.x1, mc.x1)));
                int64_t yo = static_cast<int64_t>(std::max(0, std::min(defaultCell.y2, mc.y2) -
                                                              std::max(defaultCell.y1, mc.y1)));
                if (xo > 0 && yo > 0 && xo * yo >= 0.9 * std::min(mc.area(), defaultCell.area())) {
                    if (!containingCell || mc.area() < containingCell->area()) {
                        containingCell = &mc;
                    }
                }
            }

            if (containingCell) {
                row.cells.push_back(*containingCell);
            } else if (!matchingCells.empty()) {
                int32_t bestX = xLeft;
                int64_t bestDist = INT64_MAX;
                for (auto& mc : matchingCells) {
                    for (int32_t xv : {mc.x1, mc.x2}) {
                        int64_t d = std::min(std::abs(static_cast<int64_t>(xv - xLeft)),
                                             std::abs(static_cast<int64_t>(xv - xRight)));
                        if (d < bestDist) { bestDist = d; bestX = xv; }
                    }
                }
                row.cells.push_back(Cell(bestX, yTop, bestX, yBot));
            } else {
                row.cells.push_back(defaultCell);
            }
        }
        table.rows.push_back(row);
    }

    return table;
}

} // namespace table
