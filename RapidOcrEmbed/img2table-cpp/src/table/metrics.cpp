#include "table/metrics.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <map>

namespace table {

static inline int64_t maxL(int64_t a, int64_t b) { return a >= b ? a : b; }
static inline int64_t minL(int64_t a, int64_t b) { return a <= b ? a : b; }
static inline double maxD(double a, double b) { return a >= b ? a : b; }
static inline double minD(double a, double b) { return a <= b ? a : b; }

struct CCStats {
    int32_t x, y, w, h, area;
};

static std::vector<CCStats> removeDots(const cv::Mat& ccLabels, const cv::Mat& stats) {
    std::vector<CCStats> out;
    int nLabels = stats.rows;

    for (int idx = 1; idx < nLabels; idx++) {
        int32_t x = stats.at<int>(idx, 0);
        int32_t y = stats.at<int>(idx, 1);
        int32_t w = stats.at<int>(idx, 2);
        int32_t h = stats.at<int>(idx, 3);
        int32_t area = stats.at<int>(idx, 4);

        if (w <= 0 || h <= 0 || area <= 0) continue;

        int64_t innerPixels = 0;
        for (int row = y; row < y + h; row++) {
            int64_t prevPos = -1;
            for (int col = x; col < x + w; col++) {
                if (row < ccLabels.rows && col < ccLabels.cols && ccLabels.at<int>(row, col) == idx) {
                    if (prevPos >= 0) innerPixels += col - prevPos - 1;
                    prevPos = col;
                }
            }
        }
        for (int col = x; col < x + w; col++) {
            int64_t prevPos = -1;
            for (int row = y; row < y + h; row++) {
                if (row < ccLabels.rows && col < ccLabels.cols && ccLabels.at<int>(row, col) == idx) {
                    if (prevPos >= 0) innerPixels += row - prevPos - 1;
                    prevPos = row;
                }
            }
        }

        double maxDim = static_cast<double>(std::max(w, h));
        double roundness = 4.0 * area / (CV_PI * maxDim * maxDim);

        if (!(innerPixels / (2.0 * area) <= 0.05 && roundness >= 0.7)) {
            out.push_back({x, y, w, h, area});
        }
    }
    return out;
}

static double computeIntervalUnionLength(double* starts, double* ends, int count) {
    if (count == 0) return 0.0;

    std::vector<std::pair<double, double>> intervals(count);
    for (int i = 0; i < count; i++) {
        intervals[i] = {starts[i], ends[i]};
    }
    std::sort(intervals.begin(), intervals.end());

    double curStart = intervals[0].first;
    double curEnd = intervals[0].second;
    double unionLen = 0.0;

    for (int i = 1; i < count; i++) {
        double s = intervals[i].first;
        double e = intervals[i].second;
        if (s <= curEnd) {
            curEnd = maxD(curEnd, e);
        } else {
            unionLen += curEnd - curStart;
            curStart = s;
            curEnd = e;
        }
    }
    return unionLen + curEnd - curStart;
}

static std::vector<CCStats> removeDottedLines(const std::vector<CCStats>& statsIn) {
    if (statsIn.empty()) return statsIn;

    std::vector<CCStats> result;

    auto processDirection = [&](bool horizontal) {
        std::vector<int> order(statsIn.size());
        std::iota(order.begin(), order.end(), 0);
        if (horizontal) {
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                double ma = (2.0 * statsIn[a].y + statsIn[a].h) / 2.0;
                double mb = (2.0 * statsIn[b].y + statsIn[b].h) / 2.0;
                return ma < mb;
            });
        } else {
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                double ma = (2.0 * statsIn[a].x + statsIn[a].w) / 2.0;
                double mb = (2.0 * statsIn[b].x + statsIn[b].w) / 2.0;
                return ma < mb;
            });
        }

        std::vector<double> startPos(statsIn.size()), endPos(statsIn.size());
        std::vector<std::array<double, 4>> lineAreas;
        double prevMiddle = -10.0;
        int areaCount = 0;
        double x1a = 0, y1a = 0, x2a = 0, y2a = 0;

        auto checkArea = [&]() {
            double* sPtr = startPos.data();
            double* ePtr = endPos.data();
            double unionLen = computeIntervalUnionLength(sPtr, ePtr, areaCount);
            double span = horizontal ? (x2a - x1a) : (y2a - y1a);
            if (areaCount >= 5 && unionLen / (span == 0 ? 1.0 : span) >= 0.66) {
                lineAreas.push_back({x1a, y1a, x2a, y2a});
            }
        };

        for (int oi = 0; oi < static_cast<int>(order.size()); oi++) {
            int idx = order[oi];
            const auto& s = statsIn[idx];
            double w = s.w, h = s.h;
            double middle = horizontal
                ? (2.0 * s.y + s.h) / 2.0
                : (2.0 * s.x + s.w) / 2.0;

            bool skip = horizontal ? (w / h < 2.0 || h > 5) : (h / w < 2.0 || w > 5);
            if (skip) continue;

            double x = s.x, y = s.y;
            double projStart = horizontal ? x : y;
            double projEnd = horizontal ? (x + w) : (y + h);
            if (areaCount == 0) {
                x1a = x; y1a = y; x2a = x + w; y2a = y + h;
                startPos[0] = projStart; endPos[0] = projEnd;
                prevMiddle = middle; areaCount = 1;
            } else if (middle - prevMiddle <= 2.0) {
                x1a = minD(x, x1a); y1a = minD(y, y1a);
                x2a = maxD(x + w, x2a); y2a = maxD(y + h, y2a);
                startPos[areaCount] = projStart; endPos[areaCount] = projEnd;
                areaCount++;
                prevMiddle = middle;
            } else {
                checkArea();
                x1a = x; y1a = y; x2a = x + w; y2a = y + h;
                startPos[0] = projStart; endPos[0] = projEnd;
                prevMiddle = middle; areaCount = 1;
            }
        }
        if (areaCount > 0) checkArea();
        return lineAreas;
    };

    auto hAreas = processDirection(true);
    auto vAreas = processDirection(false);

    std::vector<std::array<double, 4>> allAreas;
    allAreas.insert(allAreas.end(), hAreas.begin(), hAreas.end());
    allAreas.insert(allAreas.end(), vAreas.begin(), vAreas.end());

    if (allAreas.empty()) {
        return statsIn;
    }

    for (const auto& s : statsIn) {
        double x = s.x, y = s.y, w = s.w, h = s.h;
        double intersectionArea = 0.0;
        for (const auto& a : allAreas) {
            double xo = maxD(0.0, minD(a[2], x + w) - maxD(a[0], x));
            double yo = maxD(0.0, minD(a[3], y + h) - maxD(a[1], y));
            intersectionArea += xo * yo;
        }
        if (intersectionArea / (w * h) < 0.25) {
            result.push_back(s);
        }
    }
    return result;
}

struct FilterResult {
    std::vector<CCStats> kept;
    std::vector<CCStats> discarded;
};

static FilterResult filterCC(const std::vector<CCStats>& stats) {
    FilterResult result;
    std::vector<CCStats> keptFirst;

    for (const auto& s : stats) {
        double ar = static_cast<double>(std::max(s.w, s.h)) / std::max(1, std::min(s.w, s.h));
        double fill = static_cast<double>(s.area) / std::max(1, s.w * s.h);
        if (ar <= 5.0 && fill > 0.08) {
            keptFirst.push_back(s);
        } else {
            result.discarded.push_back(s);
        }
    }

    if (keptFirst.empty()) {
        result.kept = keptFirst;
        return result;
    }

    std::vector<int32_t> widths, heights;
    for (auto& s : keptFirst) { widths.push_back(s.w); heights.push_back(s.h); }
    std::sort(widths.begin(), widths.end());
    std::sort(heights.begin(), heights.end());
    size_t nw = widths.size(), nh = heights.size();
    double medianW = (nw % 2 == 1) ? widths[nw / 2] : (widths[nw / 2 - 1] + widths[nw / 2]) / 2.0;
    double medianH = (nh % 2 == 1) ? heights[nh / 2] : (heights[nh / 2 - 1] + heights[nh / 2]) / 2.0;
    double upper = 5.0 * medianW * medianH;
    double lower = 0.2 * medianW * medianH;

    for (auto& s : keptFirst) {
        double bw = static_cast<double>(s.w) * s.h;
        bool bounded = bw >= lower && bw <= upper;
        bool isDash = (static_cast<double>(s.w) / std::max(1, s.h) >= 2.0) &&
                      (0.5 * medianW <= s.w && s.w <= 1.5 * medianW);
        if (bounded || isDash) {
            result.kept.push_back(s);
        } else {
            result.discarded.push_back(s);
        }
    }
    return result;
}

struct CharThreshResult {
    cv::Mat characterThresh;
    std::vector<CCStats> relevantChars;
};

static CharThreshResult createCharacterThresh(
    const cv::Mat& thresh, const std::vector<CCStats>& stats,
    const std::vector<CCStats>& discarded, double charLength)
{
    cv::Mat charThresh = cv::Mat::zeros(thresh.rows, thresh.cols, CV_8UC1);
    std::vector<CCStats> relevant;

    for (const auto& s : stats) {
        relevant.push_back(s);
        for (int r = s.y; r < s.y + s.h && r < thresh.rows; r++) {
            for (int c = s.x; c < s.x + s.w && c < thresh.cols; c++) {
                charThresh.at<uchar>(r, c) = thresh.at<uchar>(r, c);
            }
        }

        for (int di = 1; di < static_cast<int>(discarded.size()); di++) {
            const auto& d = discarded[di];
            double yOverlap = static_cast<double>(minL(d.y + d.h, s.y + s.h) - maxL(d.y, s.y));
            if (yOverlap < 0.5 * minL(d.h, s.h)) continue;
            if (maxL(d.h, d.w) > 3 * maxL(s.h, s.w)) continue;

            double dist = std::abs(static_cast<double>(d.x - s.x));
            dist = minD(dist, std::abs(static_cast<double>(d.x - s.x - s.w)));
            dist = minD(dist, std::abs(static_cast<double>(d.x + d.w - s.x)));
            dist = minD(dist, std::abs(static_cast<double>(d.x + d.w - s.x - s.w)));

            if (yOverlap > 0.0 && dist <= charLength) {
                relevant.push_back(d);
                for (int r = d.y; r < d.y + d.h && r < thresh.rows; r++) {
                    for (int c = d.x; c < d.x + d.w && c < thresh.cols; c++) {
                        charThresh.at<uchar>(r, c) = thresh.at<uchar>(r, c);
                    }
                }
            }
        }
    }
    return {charThresh, relevant};
}

static cv::Mat identifyObstacles(
    const std::vector<CCStats>& stats, double minWidth, double minHeight,
    int height, int width)
{
    int kw = std::max(1, static_cast<int>(std::ceil(minWidth)));
    int kh = std::max(1, static_cast<int>(std::ceil(minHeight)));

    cv::Mat obstacles = cv::Mat::zeros(height, width, CV_8UC1);
    if (height <= 0 || width <= 0 || stats.empty()) return obstacles;
    if (kw > width || kh > height) return obstacles;

    cv::Mat occDiff = cv::Mat::zeros(height + 1, width + 1, CV_32SC1);

    for (const auto& s : stats) {
        int x1 = std::max(0, std::min(width, s.x));
        int y1 = std::max(0, std::min(height, s.y));
        int x2 = std::max(0, std::min(width, s.x + s.w));
        int y2 = std::max(0, std::min(height, s.y + s.h));
        if (x1 >= x2 || y1 >= y2) continue;
        occDiff.at<int>(y1, x1) += 1;
        occDiff.at<int>(y1, x2) -= 1;
        occDiff.at<int>(y2, x1) -= 1;
        occDiff.at<int>(y2, x2) += 1;
    }

    cv::Mat emptyPrefix = cv::Mat::zeros(height + 1, width + 1, CV_32SC1);
    for (int r = 0; r < height; r++) {
        int running = 0;
        for (int c = 0; c < width; c++) {
            running += occDiff.at<int>(r, c);
            int above = (r > 0) ? occDiff.at<int>(r - 1, c) : 0;
            int occupied = running + above;
            occDiff.at<int>(r, c) = occupied;
            int emptyCount = (occupied == 0) ? 1 : 0;
            emptyPrefix.at<int>(r + 1, c + 1) =
                emptyPrefix.at<int>(r, c + 1) + emptyPrefix.at<int>(r + 1, c) -
                emptyPrefix.at<int>(r, c) + emptyCount;
        }
    }

    cv::Mat obstDiff = cv::Mat::zeros(height + 1, width + 1, CV_32SC1);
    int windowEmpty = kw * kh;
    for (int r = 0; r <= height - kh; r++) {
        for (int c = 0; c <= width - kw; c++) {
            int ec = emptyPrefix.at<int>(r + kh, c + kw)
                   - emptyPrefix.at<int>(r, c + kw)
                   - emptyPrefix.at<int>(r + kh, c)
                   + emptyPrefix.at<int>(r, c);
            if (ec != windowEmpty) continue;
            obstDiff.at<int>(r, c) += 1;
            obstDiff.at<int>(r, c + kw) -= 1;
            obstDiff.at<int>(r + kh, c) -= 1;
            obstDiff.at<int>(r + kh, c + kw) += 1;
        }
    }

    cv::Mat obst = cv::Mat::zeros(height + 1, width + 1, CV_32SC1);
    for (int r = 0; r < height; r++) {
        int running = 0;
        for (int c = 0; c < width; c++) {
            running += obstDiff.at<int>(r, c);
            int above = (r > 0) ? obstDiff.at<int>(r - 1, c) : 0;
            obst.at<int>(r, c) = running + above;
            obstacles.at<uchar>(r, c) = (obst.at<int>(r, c) > 0) ? 1 : 0;
        }
    }

    return obstacles;
}

static std::vector<CCStats> identifyCloseContours(
    const std::vector<CCStats>& chars, const cv::Mat& obstacles, double charLength)
{
    int count = static_cast<int>(chars.size());
    if (count == 0) return {};

    std::vector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return chars[a].x < chars[b].x;
    });

    std::vector<int> parent(count), sz(count, 1);
    for (int i = 0; i < count; i++) parent[i] = i;

    std::function<int(int)> findRoot = [&](int idx) -> int {
        int root = idx;
        while (parent[root] != root) root = parent[root];
        while (parent[idx] != idx) {
            int p = parent[idx];
            parent[idx] = root;
            idx = p;
        }
        return root;
    };

    auto unionRoots = [&](int i, int j) {
        int r1 = findRoot(i);
        int r2 = findRoot(j);
        if (r1 == r2) return;
        if (sz[r1] < sz[r2]) std::swap(r1, r2);
        parent[r2] = r1;
        sz[r1] += sz[r2];
    };

    cv::Mat obstaclePrefix = cv::Mat::zeros(obstacles.rows + 1, obstacles.cols + 1, CV_32SC1);
    for (int r = 0; r < obstacles.rows; r++) {
        int rowSum = 0;
        for (int c = 0; c < obstacles.cols; c++) {
            rowSum += obstacles.at<uchar>(r, c);
            obstaclePrefix.at<int>(r + 1, c + 1) = obstaclePrefix.at<int>(r, c + 1) + rowSum;
        }
    }

    for (int posI = 0; posI < count; posI++) {
        int i = order[posI];
        int64_t xi = chars[i].x, yi = chars[i].y, wi = chars[i].w, hi = chars[i].h;

        for (int posJ = posI + 1; posJ < count; posJ++) {
            int j = order[posJ];
            int64_t xj = chars[j].x;
            if (xj > xi + wi + charLength) break;

            int64_t yj = chars[j].y, wj = chars[j].w, hj = chars[j].h;
            int64_t xOverlap = minL(xi + wi, xj + wj) - maxL(xi, xj);
            int64_t yOverlap = minL(yi + hi, yj + hj) - maxL(yi, yj);
            int64_t minW = minL(wi, wj);
            int64_t minH = minL(hi, hj);

            if (xOverlap > 0 && yOverlap > 0) {
                unionRoots(i, j);
            } else if (2 * yOverlap >= minH && xOverlap > -charLength) {
                int x1 = static_cast<int>(minL(xi + wi, xj + wj));
                int y1 = static_cast<int>(maxL(yi, yj));
                int x2 = static_cast<int>(maxL(xi, xj));
                int y2 = static_cast<int>(minL(yi + hi, yj + hj));
                if (x1 < x2 && y1 < y2) {
                    int obSum = obstaclePrefix.at<int>(y2, x2) - obstaclePrefix.at<int>(y1, x2)
                              - obstaclePrefix.at<int>(y2, x1) + obstaclePrefix.at<int>(y1, x1);
                    if (obSum * 10 < (x2 - x1) * (y2 - y1)) {
                        unionRoots(i, j);
                    }
                }
            } else if (2 * xOverlap >= minW && yOverlap > -charLength) {
                int x1 = static_cast<int>(maxL(xi, xj));
                int y1 = static_cast<int>(minL(yi + hi, yj + hj));
                int x2 = static_cast<int>(minL(xi + wi, xj + wj));
                int y2 = static_cast<int>(maxL(yi, yj));
                if (x1 < x2 && y1 < y2) {
                    int obSum = obstaclePrefix.at<int>(y2, x2) - obstaclePrefix.at<int>(y1, x2)
                              - obstaclePrefix.at<int>(y2, x1) + obstaclePrefix.at<int>(y1, x1);
                    if (obSum * 10 < (x2 - x1) * (y2 - y1)) {
                        unionRoots(i, j);
                    }
                }
            }
        }
    }

    std::vector<CCStats> result(count);
    for (int i = 0; i < count; i++) {
        result[i] = chars[findRoot(i)];
    }
    return result;
}

static std::vector<CCStats> computeContours(
    const std::vector<CCStats>& chars, const cv::Mat& obstacles, double charLength)
{
    std::vector<CCStats> merged = identifyCloseContours(chars, obstacles, charLength);

    std::map<std::pair<int,int>, int> compMap;
    std::vector<std::array<int64_t, 4>> bounds;
    int compCount = 0;

    for (int i = 0; i < static_cast<int>(chars.size()); i++) {
        int rootX = merged[i].x, rootY = merged[i].y;

        int64_t x1 = chars[i].x, y1 = chars[i].y;
        int64_t x2 = x1 + chars[i].w, y2 = y1 + chars[i].h;

        auto key = std::make_pair(rootX, rootY);
        auto it = compMap.find(key);
        if (it == compMap.end()) {
            compMap[key] = compCount;
            bounds.push_back({x1, y1, x2, y2});
            compCount++;
        } else {
            auto& b = bounds[it->second];
            if (x1 < b[0]) b[0] = x1;
            if (y1 < b[1]) b[1] = y1;
            if (x2 > b[2]) b[2] = x2;
            if (y2 > b[3]) b[3] = y2;
        }
    }

    std::vector<CCStats> out(compCount);
    for (int i = 0; i < compCount; i++) {
        out[i] = {
            static_cast<int32_t>(bounds[i][0]),
            static_cast<int32_t>(bounds[i][1]),
            static_cast<int32_t>(bounds[i][2] - bounds[i][0]),
            static_cast<int32_t>(bounds[i][3] - bounds[i][1]),
            0
        };
    }
    return out;
}

static std::vector<double> getRowSeparations(const std::vector<CCStats>& stats, double charLength) {
    std::vector<double> out;
    double halfChar = std::floor(charLength / 2.0);

    for (size_t i = 0; i < stats.size(); i++) {
        double rowSep = 1e6;
        int64_t xi = stats[i].x, yi = stats[i].y, wi = stats[i].w, hi = stats[i].h;
        for (size_t j = 0; j < stats.size(); j++) {
            if (i == j) continue;
            int64_t xj = stats[j].x, yj = stats[j].y, wj = stats[j].w, hj = stats[j].h;
            int64_t hOverlap = minL(xi + wi, xj + wj) - maxL(xi, xj);
            double vPosI = (2.0 * yi + hi) / 2.0;
            double vPosJ = (2.0 * yj + hj) / 2.0;
            if (hOverlap <= halfChar || vPosJ <= vPosI) continue;
            rowSep = minD(rowSep, vPosJ - vPosI);
        }
        if (rowSep < 1e6) out.push_back(rowSep);
    }
    return out;
}

bool computeImgMetrics(const cv::Mat& thresh, ImgMetrics& metrics) {
    cv::Mat labels, statsMat, centroids;
    int nLabels = cv::connectedComponentsWithStats(thresh, labels, statsMat, centroids, 8, CV_32S);

    auto filtered = removeDots(labels, statsMat);

    std::vector<CCStats> areaFiltered;
    for (auto& s : filtered) {
        if (s.area > 5) areaFiltered.push_back(s);
    }
    if (areaFiltered.empty()) return false;

    std::vector<CCStats> completeStats(areaFiltered.size());
    for (size_t i = 0; i < areaFiltered.size(); i++) {
        completeStats[i] = areaFiltered[i];
    }
    auto afterDotted = removeDottedLines(completeStats);
    if (afterDotted.empty()) return false;

    auto [relevantStats, discardedStats] = filterCC(afterDotted);

    if (relevantStats.empty()) return false;

    // Compute char length — find mode of widths (most frequent width)
    int maxW = 0;
    for (auto& s : relevantStats) maxW = std::max(maxW, s.w);
    std::vector<int> bincount(maxW + 1, 0);
    for (auto& s : relevantStats) bincount[s.w]++;
    int modeW = 0;
    for (int i = 1; i <= maxW; i++) {
        if (bincount[i] > bincount[modeW]) modeW = i;
    }
    double argmaxCharLength = static_cast<double>(modeW);

    double sumW = 0;
    for (auto& s : relevantStats) sumW += s.w;
    double meanCharLength = sumW / relevantStats.size();

    double charLength = (1.5 * argmaxCharLength <= meanCharLength) ? meanCharLength : argmaxCharLength;

    auto [charThresh, charsArray] = createCharacterThresh(thresh, relevantStats, discardedStats, charLength);

    // Compute median line sep
    cv::Mat vObs = identifyObstacles(charsArray, charLength / 3.0, thresh.rows / 5.0,
                                      thresh.rows, thresh.cols);
    cv::Mat hObs = identifyObstacles(charsArray, thresh.cols / 5.0, charLength / 4.0,
                                      thresh.rows, thresh.cols);

    cv::Mat obstacles = cv::Mat::zeros(thresh.rows, thresh.cols, CV_8UC1);
    for (int r = 0; r < thresh.rows; r++) {
        for (int c = 0; c < thresh.cols; c++) {
            uchar v = std::max(vObs.at<uchar>(r, c), hObs.at<uchar>(r, c));
            obstacles.at<uchar>(r, c) = v;
        }
    }

    auto contourStats = computeContours(charsArray, obstacles, charLength);

    auto rowSeps = getRowSeparations(contourStats, charLength);

    double medianLineSep = 0;
    if (!rowSeps.empty()) {
        std::vector<double> binned(rowSeps.size());
        for (size_t i = 0; i < rowSeps.size(); i++) {
            binned[i] = 2.0 * std::floor(rowSeps[i] / 2.0) + 1.0;
        }
        // Find mode (most frequent binned value)
        std::sort(binned.begin(), binned.end());
        double bestVal = binned[0];
        int bestCount = 1;
        int curCount = 1;
        for (size_t i = 1; i < binned.size(); i++) {
            if (binned[i] == binned[i-1]) {
                curCount++;
            } else {
                if (curCount > bestCount) {
                    bestCount = curCount;
                    bestVal = binned[i-1];
                }
                curCount = 1;
            }
        }
        if (curCount > bestCount) {
            bestVal = binned.back();
        }
        medianLineSep = bestVal;
    }

    std::vector<Cell> contourCells;
    for (auto& s : contourStats) {
        contourCells.push_back(Cell(s.x, s.y, s.x + s.w, s.y + s.h));
    }

    metrics.charLength = charLength;
    metrics.medianLineSep = medianLineSep;
    metrics.contours = contourCells;
    return true;
}

} // namespace table
