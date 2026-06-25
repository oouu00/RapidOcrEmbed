#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <opencv2/core.hpp>

namespace table {

struct Line {
    int32_t x1, y1, x2, y2;
    int32_t thickness = 1;

    double angle() const {
        double dx = x2 - x1;
        double dy = y2 - y1;
        return std::atan2(dy, dx) * 180.0 / CV_PI;
    }

    double length() const {
        double h = y2 - y1;
        double w = x2 - x1;
        return std::sqrt(h * h + w * w);
    }

    bool horizontal() const {
        double a = angle();
        return std::fmod(std::abs(a), 180.0) < 1e-6;
    }

    bool vertical() const {
        double a = angle();
        double mod = std::fmod(std::abs(a), 180.0);
        return std::abs(mod - 90.0) < 1e-6;
    }

    Line& reprocess() {
        int32_t nx1 = std::min(x1, x2);
        int32_t nx2 = std::max(x1, x2);
        int32_t ny1 = std::min(y1, y2);
        int32_t ny2 = std::max(y1, y2);
        x1 = nx1; x2 = nx2; y1 = ny1; y2 = ny2;

        double a = angle();
        if (std::abs(a) <= 5.0) {
            int32_t yv = static_cast<int32_t>(std::round((y1 + y2) / 2.0));
            y1 = yv; y2 = yv;
        } else if (std::abs(a - 90.0) <= 5.0) {
            int32_t xv = static_cast<int32_t>(std::round((x1 + x2) / 2.0));
            x1 = xv; x2 = xv;
        }
        return *this;
    }

    bool operator==(const Line& o) const {
        return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
    }
};

struct Cell {
    int32_t x1, y1, x2, y2;
    std::string content;

    Cell() : x1(0), y1(0), x2(0), y2(0) {}
    Cell(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
        : x1(x1), y1(y1), x2(x2), y2(y2) {}

    int32_t height() const { return y2 - y1; }
    int32_t width() const { return x2 - x1; }
    int64_t area() const { return static_cast<int64_t>(height()) * width(); }

    bool operator==(const Cell& o) const {
        return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
    }
    bool operator<(const Cell& o) const {
        if (x1 != o.x1) return x1 < o.x1;
        if (y1 != o.y1) return y1 < o.y1;
        if (x2 != o.x2) return x2 < o.x2;
        return y2 < o.y2;
    }
};

struct Row {
    std::vector<Cell> cells;

    int32_t x1() const {
        if (cells.empty()) return 0;
        int32_t v = cells[0].x1;
        for (auto& c : cells) v = std::min(v, c.x1);
        return v;
    }
    int32_t y1() const {
        if (cells.empty()) return 0;
        int32_t v = cells[0].y1;
        for (auto& c : cells) v = std::min(v, c.y1);
        return v;
    }
    int32_t x2() const {
        if (cells.empty()) return 0;
        int32_t v = cells[0].x2;
        for (auto& c : cells) v = std::max(v, c.x2);
        return v;
    }
    int32_t y2() const {
        if (cells.empty()) return 0;
        int32_t v = cells[0].y2;
        for (auto& c : cells) v = std::max(v, c.y2);
        return v;
    }

    size_t nbColumns() const { return cells.size(); }

    bool vConsistent() const {
        if (cells.empty()) return true;
        for (auto& c : cells) {
            if (c.y1 != cells[0].y1 || c.y2 != cells[0].y2)
                return false;
        }
        return true;
    }
};

struct Table {
    std::vector<Row> rows;

    size_t nbRows() const { return rows.size(); }
    size_t nbColumns() const { return rows.empty() ? 0 : rows[0].nbColumns(); }
    size_t nbCells() const {
        std::set<Cell> uniqueCells;
        for (auto& r : rows)
            for (auto& c : r.cells)
                uniqueCells.insert(c);
        return uniqueCells.size();
    }

    int32_t x1() const {
        if (rows.empty()) return 0;
        int32_t v = rows[0].x1();
        for (auto& r : rows) v = std::min(v, r.x1());
        return v;
    }
    int32_t y1() const {
        if (rows.empty()) return 0;
        int32_t v = rows[0].y1();
        for (auto& r : rows) v = std::min(v, r.y1());
        return v;
    }
    int32_t x2() const {
        if (rows.empty()) return 0;
        int32_t v = rows[0].x2();
        for (auto& r : rows) v = std::max(v, r.x2());
        return v;
    }
    int32_t y2() const {
        if (rows.empty()) return 0;
        int32_t v = rows[0].y2();
        for (auto& r : rows) v = std::max(v, r.y2());
        return v;
    }

    int64_t area() const {
        return static_cast<int64_t>(y2() - y1()) * (x2() - x1());
    }

    bool hasValidShape() const {
        if (std::min(nbRows(), nbColumns()) < 2 || nbCells() < 4)
            return false;

        std::set<Cell> cellSet;
        for (auto& r : rows)
            for (auto& c : r.cells)
                cellSet.insert(c);
        std::vector<Cell> cells(cellSet.begin(), cellSet.end());

        std::sort(cells.begin(), cells.end(),
            [](const Cell& a, const Cell& b) { return a.area() > b.area(); });

        std::set<int32_t> xvals, yvals;
        for (auto& c : cells) {
            xvals.insert(c.x1); xvals.insert(c.x2);
            yvals.insert(c.y1); yvals.insert(c.y2);
        }
        if (xvals.size() > nbColumns() + 1) return false;
        if (yvals.size() > nbRows() + 1) return false;

        int64_t pairOverlap = 0;
        for (size_t i = 0; i < cells.size(); i++) {
            for (size_t j = i + 1; j < cells.size(); j++) {
                int64_t min_x2 = std::min(cells[i].x2, cells[j].x2);
                int64_t max_x1 = std::max(cells[i].x1, cells[j].x1);
                int64_t min_y2 = std::min(cells[i].y2, cells[j].y2);
                int64_t max_y1 = std::max(cells[i].y1, cells[j].y1);
                int64_t xo = min_x2 > max_x1 ? min_x2 - max_x1 : 0;
                int64_t yo = min_y2 > max_y1 ? min_y2 - max_y1 : 0;
                pairOverlap += xo * yo;
            }
        }

        return (2 * pairOverlap) < (area() / 5);
    }
};

struct ImgMetrics {
    double charLength = 0;
    double medianLineSep = 0;
    std::vector<Cell> contours;
};

} // namespace table
