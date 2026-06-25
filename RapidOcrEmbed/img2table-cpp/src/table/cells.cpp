#include "table/cells.h"
#include <algorithm>
#include <vector>

namespace table {

std::vector<Cell> getCellsFromLines(const std::vector<Line>& hLines,
                                    const std::vector<Line>& vLines)
{
    if (hLines.empty() || vLines.empty()) return {};

    // Generate potential cells from horizontal line pairs
    struct PotentialCell {
        int64_t x1, x2, y1, y2;
    };
    std::vector<PotentialCell> potentials;

    for (size_t i = 0; i < hLines.size(); i++) {
        for (size_t j = 0; j < hLines.size(); j++) {
            int64_t x1i = hLines[i].x1, y1i = hLines[i].y1, x2i = hLines[i].x2;
            int64_t x1j = hLines[j].x1, y1j = hLines[j].y1;
            int64_t x2j = hLines[j].x2, y2j = hLines[j].y2;

            if (y1i >= y1j) continue;

            double w = static_cast<double>(x2i - x1i);
            if (w == 0) w = 1.0;
            bool lCorresponds = ((x1i - x1j) / w >= -0.02 && (x1i - x1j) / w <= 0.02);
            bool rCorresponds = ((x2i - x2j) / w >= -0.02 && (x2i - x2j) / w <= 0.02);
            bool lContained = (x1i <= x1j && x1j <= x2i) || (x1j <= x1i && x1i <= x2j);
            bool rContained = (x1i <= x2j && x2j <= x2i) || (x1j <= x2i && x2i <= x2j);

            if ((lCorresponds || lContained) && (rCorresponds || rContained)) {
                potentials.push_back({std::max(x1i, x1j), std::min(x2i, x2j), y1i, y2j});
            }
        }
    }

    if (potentials.empty()) return {};

    // Deduplicate on upper bound
    std::sort(potentials.begin(), potentials.end(), [](const PotentialCell& a, const PotentialCell& b) {
        if (a.x1 != b.x1) return a.x1 < b.x1;
        if (a.x2 != b.x2) return a.x2 < b.x2;
        if (a.y1 != b.y1) return a.y1 < b.y1;
        return a.y2 < b.y2;
    });

    std::vector<PotentialCell> dedupUpper;
    int64_t prevX1 = 0, prevX2 = 0, prevY1 = 0;
    for (auto& p : potentials) {
        if (!(p.x1 == prevX1 && p.x2 == prevX2 && p.y1 == prevY1)) {
            dedupUpper.push_back({p.x1, p.x2, p.y2, -p.y1});
        }
        prevX1 = p.x1; prevX2 = p.x2; prevY1 = p.y1;
    }

    // Deduplicate on lower bound
    std::sort(dedupUpper.begin(), dedupUpper.end(), [](const PotentialCell& a, const PotentialCell& b) {
        if (a.x1 != b.x1) return a.x1 < b.x1;
        if (a.x2 != b.x2) return a.x2 < b.x2;
        if (a.y1 != b.y1) return a.y1 < b.y1;
        return a.y2 < b.y2;
    });

    std::vector<PotentialCell> dedupLower;
    prevX1 = 0; int64_t prevX2b = 0, prevY2 = 0;
    for (auto& p : dedupUpper) {
        int64_t realY1 = -p.y2;
        if (!(p.x1 == prevX1 && p.x2 == prevX2b && p.y1 == prevY2)) {
            dedupLower.push_back({p.x1, p.x2, realY1, p.y1});
        }
        prevX1 = p.x1; prevX2b = p.x2; prevY2 = p.y1;
    }

    // Match with vertical lines
    std::vector<Cell> cells;
    for (auto& d : dedupLower) {
        int64_t dx1 = d.x1, dx2 = d.x2, dy1 = d.y1, dy2 = d.y2;
        int64_t margin = std::max(static_cast<int64_t>(5),
                                   static_cast<int64_t>((dx2 - dx1) * 0.025));

        std::vector<int64_t> delimiters;
        for (auto& v : vLines) {
            int64_t x1v = v.x1, y1v = v.y1, y2v = v.y2;
            if (dx1 - margin <= x1v && x1v <= dx2 + margin) {
                int64_t overlap = std::min(dy2, y2v) - std::max(dy1, y1v);
                int64_t tolerance = std::max(static_cast<int64_t>(5),
                                              std::min(static_cast<int64_t>(10),
                                                       static_cast<int64_t>(0.1 * (dy2 - dy1))));
                if (dy2 - dy1 - overlap <= tolerance) {
                    delimiters.push_back(x1v);
                }
            }
        }

        if (delimiters.size() >= 2) {
            std::sort(delimiters.begin(), delimiters.end());
            for (size_t j = 0; j < delimiters.size() - 1; j++) {
                cells.push_back(Cell(static_cast<int32_t>(delimiters[j]),
                                     static_cast<int32_t>(dy1),
                                     static_cast<int32_t>(delimiters[j + 1]),
                                     static_cast<int32_t>(dy2)));
            }
        }
    }

    return cells;
}

std::vector<Cell> deduplicateCells(const std::vector<Cell>& cellsIn) {
    if (cellsIn.empty()) return {};

    int32_t xMax = 0, yMax = 0;
    for (auto& c : cellsIn) {
        xMax = std::max(xMax, c.x2);
        yMax = std::max(yMax, c.y2);
    }
    if (xMax <= 0 || yMax <= 0) return cellsIn;

    std::vector<Cell> sortedCells = cellsIn;
    std::sort(sortedCells.begin(), sortedCells.end(),
              [](const Cell& a, const Cell& b) { return a.area() < b.area(); });

    cv::Mat coverage = cv::Mat::ones(yMax, xMax, CV_8UC1);

    std::vector<Cell> result;
    for (auto& c : sortedCells) {
        int y1 = std::max(0, c.y1);
        int y2 = std::min(yMax, c.y2);
        int x1 = std::max(0, c.x1);
        int x2 = std::min(xMax, c.x2);
        if (y2 <= y1 || x2 <= x1) continue;

        int sum = static_cast<int>(cv::sum(coverage(cv::Rect(x1, y1, x2 - x1, y2 - y1)))[0]);
        if (sum >= 0.25 * c.area()) {
            result.push_back(c);
            coverage(cv::Rect(x1, y1, x2 - x1, y2 - y1)).setTo(0);
        }
    }
    return result;
}

} // namespace table
