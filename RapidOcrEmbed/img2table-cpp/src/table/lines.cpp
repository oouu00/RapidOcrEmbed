#include "table/lines.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace table {

static std::vector<Line> identifyStraightLines(
    const cv::Mat& thresh, int minLineLength, double charLength, bool vertical)
{
    int openLen = vertical ? (static_cast<int>(std::round(minLineLength / 3.0)) || 1)
                           : (static_cast<int>(std::round(minLineLength / 3.0)) || 1);
    cv::Size kSize = vertical ? cv::Size(1, openLen) : cv::Size(openLen, 1);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, kSize);
    cv::Mat mask;
    cv::morphologyEx(thresh, mask, cv::MORPH_OPEN, kernel);

    cv::Size hollowSize = vertical ? cv::Size(3, 1) : cv::Size(1, 3);
    cv::Mat hollowKernel = cv::getStructuringElement(cv::MORPH_RECT, hollowSize);
    cv::Mat maskClosed;
    cv::morphologyEx(mask, maskClosed, cv::MORPH_CLOSE, hollowKernel);

    int dottedLen = vertical ? (static_cast<int>(std::round(minLineLength / 6.0)) || 1)
                             : (static_cast<int>(std::round(minLineLength / 6.0)) || 1);
    cv::Size dottedSize = vertical ? cv::Size(1, dottedLen) : cv::Size(dottedLen, 1);
    cv::Mat dottedKernel = cv::getStructuringElement(cv::MORPH_RECT, dottedSize);
    cv::Mat maskDotted;
    cv::morphologyEx(maskClosed, maskDotted, cv::MORPH_CLOSE, dottedKernel);

    cv::Size finalSize = vertical ? cv::Size(1, std::max(1, minLineLength))
                                  : cv::Size(std::max(1, minLineLength), 1);
    cv::Mat finalKernel = cv::getStructuringElement(cv::MORPH_RECT, finalSize);
    cv::Mat finalMask;
    cv::morphologyEx(maskDotted, finalMask, cv::MORPH_OPEN, finalKernel);

    cv::Mat labels, stats, centroids;
    int nLabels = cv::connectedComponentsWithStats(finalMask, labels, stats, centroids, 8, CV_32S);

    std::vector<Line> lines;
    for (int idx = 1; idx < nLabels; idx++) {
        int x = stats.at<int>(idx, cv::CC_STAT_LEFT);
        int y = stats.at<int>(idx, cv::CC_STAT_TOP);
        int w = stats.at<int>(idx, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(idx, cv::CC_STAT_HEIGHT);

        if (std::max(w, h) / std::max(1, std::min(w, h)) < 5 &&
            std::min(w, h) >= charLength) continue;
        if (std::max(w, h) < minLineLength) continue;

        cv::Mat cropped = thresh(cv::Rect(x, y, w, h));
        Line line;

        if (w >= h) {
            std::vector<int> colSums(w, 0);
            for (int r = 0; r < h; r++) {
                for (int c = 0; c < w; c++) {
                    colSums[c] += cropped.at<uchar>(r, c);
                }
            }
            std::vector<int> nonBlank;
            for (int c = 0; c < w; c++) {
                if (colSums[c] > 0) nonBlank.push_back(c);
            }
            if (nonBlank.empty()) continue;

            std::vector<int> lineRows;
            for (int r = 0; r < h; r++) {
                int sum = 0;
                for (int c = 0; c < w; c++) sum += (cropped.at<uchar>(r, c) > 0) ? 1 : 0;
                if (sum >= 0.5 * w) lineRows.push_back(r);
            }
            if (lineRows.empty()) continue;

            double meanY = 0;
            for (auto v : lineRows) meanY += v;
            meanY /= lineRows.size();

            line.x1 = x + nonBlank.front();
            line.y1 = y + static_cast<int32_t>(std::round(meanY));
            line.x2 = x + nonBlank.back();
            line.y2 = line.y1;
            line.thickness = static_cast<int32_t>(lineRows.back() - lineRows.front() + 1);
        } else {
            std::vector<int> rowSums(h, 0);
            for (int r = 0; r < h; r++) {
                for (int c = 0; c < w; c++) {
                    rowSums[r] += cropped.at<uchar>(r, c);
                }
            }
            std::vector<int> nonBlank;
            for (int r = 0; r < h; r++) {
                if (rowSums[r] > 0) nonBlank.push_back(r);
            }
            if (nonBlank.empty()) continue;

            std::vector<int> lineCols;
            for (int c = 0; c < w; c++) {
                int sum = 0;
                for (int r = 0; r < h; r++) sum += (cropped.at<uchar>(r, c) > 0) ? 1 : 0;
                if (sum >= 0.5 * h) lineCols.push_back(c);
            }
            if (lineCols.empty()) continue;

            double meanX = 0;
            for (auto v : lineCols) meanX += v;
            meanX /= lineCols.size();

            line.x1 = x + static_cast<int32_t>(std::round(meanX));
            line.y1 = y + nonBlank.front();
            line.x2 = line.x1;
            line.y2 = y + nonBlank.back();
            line.thickness = static_cast<int32_t>(lineCols.back() - lineCols.front() + 1);
        }
        lines.push_back(line);
    }
    return lines;
}

void detectLines(const cv::Mat& img, const std::vector<Cell>& contours,
                 double charLength, int minLineLength,
                 std::vector<Line>& hLines, std::vector<Line>& vLines)
{
    cv::Mat blurImg;
    cv::bilateralFilter(img, blurImg, 3, 40, 80);

    cv::Mat gray;
    if (blurImg.channels() == 3) {
        cv::cvtColor(blurImg, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = blurImg;
    }

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F, 3);
    cv::Mat edgeImg;
    cv::convertScaleAbs(laplacian, edgeImg);

    for (auto& c : contours) {
        int y1 = std::max(0, c.y1 - 1);
        int y2 = std::min(edgeImg.rows, c.y2 + 1);
        int x1 = std::max(0, c.x1 - 1);
        int x2 = std::min(edgeImg.cols, c.x2 + 1);
        if (y2 > y1 && x2 > x1) {
            edgeImg(cv::Rect(x1, y1, x2 - x1, y2 - y1)).setTo(0);
        }
    }

    double meanVal = cv::mean(edgeImg)[0];
    double maxVal = 0;
    cv::minMaxLoc(edgeImg, nullptr, &maxVal);
    double threshVal = std::min(2.5 * meanVal, maxVal);

    cv::Mat binaryImg = cv::Mat::zeros(edgeImg.rows, edgeImg.cols, CV_8UC1);
    for (int r = 0; r < edgeImg.rows; r++) {
        for (int c = 0; c < edgeImg.cols; c++) {
            if (edgeImg.at<uchar>(r, c) >= static_cast<uchar>(threshVal))
                binaryImg.at<uchar>(r, c) = 255;
        }
    }

    auto hLinesRaw = identifyStraightLines(binaryImg, minLineLength, charLength, false);
    auto vLinesRaw = identifyStraightLines(binaryImg, minLineLength, charLength, true);

    hLines.clear();
    vLines.clear();
    for (auto& l : hLinesRaw) {
        l.reprocess();
        if (l.horizontal()) hLines.push_back(l);
    }
    for (auto& l : vLinesRaw) {
        l.reprocess();
        if (l.vertical()) vLines.push_back(l);
    }
}

} // namespace table
