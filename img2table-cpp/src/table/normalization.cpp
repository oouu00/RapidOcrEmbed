#include "table/normalization.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace table {

std::vector<int> clusterValues(const std::vector<double>& values,
                               double medianGapMultiple, double minGap) {
    if (values.size() <= 1) return std::vector<int>(values.size(), 0);

    std::vector<double> sortedVals = values;
    std::sort(sortedVals.begin(), sortedVals.end());
    auto last = std::unique(sortedVals.begin(), sortedVals.end());
    sortedVals.erase(last, sortedVals.end());

    std::vector<double> gaps;
    for (size_t i = 1; i < sortedVals.size(); i++) {
        gaps.push_back(sortedVals[i] - sortedVals[i - 1]);
    }

    double gapThreshold;
    if (gaps.size() > 2) {
        std::vector<double> sortedGaps = gaps;
        std::sort(sortedGaps.begin(), sortedGaps.end());
        size_t n = sortedGaps.size();
        double med = (n % 2 == 1) ? sortedGaps[n / 2] : (sortedGaps[n / 2 - 1] + sortedGaps[n / 2]) / 2.0;
        gapThreshold = medianGapMultiple * med;
    } else {
        gapThreshold = medianGapMultiple * (gaps.empty() ? 0.0 : *std::min_element(gaps.begin(), gaps.end()));
    }

    std::map<double, int> clusterByValue;
    int clusterId = 0;
    clusterByValue[sortedVals[0]] = 0;
    for (size_t i = 0; i < gaps.size(); i++) {
        if (gaps[i] > std::max(gapThreshold, minGap)) {
            clusterId++;
        }
        clusterByValue[sortedVals[i + 1]] = clusterId;
    }

    std::vector<int> result;
    for (double v : values) {
        result.push_back(clusterByValue[v]);
    }
    return result;
}

std::vector<Cell> normalizeTableCells(const std::vector<Cell>& clusterCells,
                                      double charLength) {
    if (clusterCells.empty()) return {};

    std::set<int32_t> hSet, vSet;
    for (auto& c : clusterCells) {
        hSet.insert(c.x1); hSet.insert(c.x2);
        vSet.insert(c.y1); vSet.insert(c.y2);
    }

    std::vector<double> hValues(hSet.begin(), hSet.end());
    std::vector<double> vValues(vSet.begin(), vSet.end());

    auto hLabels = clusterValues(hValues, 0.1, charLength);
    auto vLabels = clusterValues(vValues, 0.1, 0.5 * charLength);

    std::map<int, std::vector<double>> hClusters, vClusters;
    for (size_t i = 0; i < hValues.size(); i++) {
        hClusters[hLabels[i]].push_back(hValues[i]);
    }
    for (size_t i = 0; i < vValues.size(); i++) {
        vClusters[vLabels[i]].push_back(vValues[i]);
    }

    std::vector<int32_t> hDelims, vDelims;
    for (auto& [_, vals] : hClusters) {
        double sum = 0;
        for (double v : vals) sum += v;
        hDelims.push_back(static_cast<int32_t>(std::round(sum / vals.size())));
    }
    for (auto& [_, vals] : vClusters) {
        double sum = 0;
        for (double v : vals) sum += v;
        vDelims.push_back(static_cast<int32_t>(std::round(sum / vals.size())));
    }
    std::sort(hDelims.begin(), hDelims.end());
    std::sort(vDelims.begin(), vDelims.end());

    auto nearest = [](const std::vector<int32_t>& delims, int32_t val) -> int32_t {
        int32_t best = delims.empty() ? 0 : delims[0];
        int64_t bestDist = std::abs(static_cast<int64_t>(val) - best);
        for (int32_t d : delims) {
            int64_t dist = std::abs(static_cast<int64_t>(val) - d);
            if (dist < bestDist) { bestDist = dist; best = d; }
        }
        return best;
    };

    std::vector<Cell> result;
    for (auto& c : clusterCells) {
        Cell nc(nearest(hDelims, c.x1), nearest(vDelims, c.y1),
                nearest(hDelims, c.x2), nearest(vDelims, c.y2));
        if (nc.area() > 0) result.push_back(nc);
    }
    return result;
}

} // namespace table
