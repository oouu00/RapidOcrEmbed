#include "table/extractor.h"
#include "table/threshold.h"
#include "table/metrics.h"
#include "table/lines.h"
#include "table/cells.h"
#include "table/clustering.h"
#include "table/normalization.h"
#include "table/creation.h"
#include "table/html_gen.h"

namespace table {

ExtractorResult extractBorderedTables(const cv::Mat& img) {
    ExtractorResult result;

    cv::Mat thresh = thresholdDarkAreas(img, 11.0);

    ImgMetrics metrics;
    if (!computeImgMetrics(thresh, metrics)) {
        return result;
    }

    double minLineLength = std::min(metrics.medianLineSep,
                                     2.0 * metrics.charLength);
    int minLen = static_cast<int>(minLineLength);

    std::vector<Line> hLines, vLines;
    detectLines(img, metrics.contours, metrics.charLength, minLen, hLines, vLines);

    std::vector<Line> allLines;
    allLines.insert(allLines.end(), hLines.begin(), hLines.end());
    allLines.insert(allLines.end(), vLines.begin(), vLines.end());

    auto cells = getCellsFromLines(hLines, vLines);
    auto deduped = deduplicateCells(cells);
    auto clusters = clusterCellsInTables(deduped);

    std::vector<Table> tables;
    for (auto& cluster : clusters) {
        if (cluster.size() <= 1) continue;
        auto normalized = normalizeTableCells(cluster, metrics.charLength);
        if (normalized.empty()) continue;
        auto tbl = clusterToTable(normalized, metrics.contours);
        if (tbl.nbRows() * tbl.nbColumns() >= 2) {
            tables.push_back(tbl);
        }
    }

    for (auto& tbl : tables) {
        if (tbl.hasValidShape()) {
            result.tables.push_back(tbl);
        }
    }

    for (auto& tbl : result.tables) {
        result.html += generateHTML(tbl);
    }

    return result;
}

} // namespace table
