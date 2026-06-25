#include "table/threshold.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace table {

static cv::Mat sauvolaThreshold(const cv::Mat& src, int kernel, double k, double R) {
    cv::Mat integralImg, integralSqImg;
    cv::integral(src, integralImg, CV_64F);
    cv::Mat srcF;
    src.convertTo(srcF, CV_64F);
    cv::Mat sq;
    cv::multiply(srcF, srcF, sq);
    cv::integral(sq, integralSqImg, CV_64F);

    int rows = src.rows;
    int cols = src.cols;
    cv::Mat thresh(rows, cols, CV_64F);

    int half = kernel / 2;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int r1 = std::max(0, r - half);
            int c1 = std::max(0, c - half);
            int r2 = std::min(rows - 1, r + half);
            int c2 = std::min(cols - 1, c + half);
            int cnt = (r2 - r1 + 1) * (c2 - c1 + 1);
            double mean = (integralImg.at<double>(r2+1, c2+1)
                         - integralImg.at<double>(r1, c2+1)
                         - integralImg.at<double>(r2+1, c1)
                         + integralImg.at<double>(r1, c1)) / cnt;
            double sqMean = (integralSqImg.at<double>(r2+1, c2+1)
                           - integralSqImg.at<double>(r1, c2+1)
                           - integralSqImg.at<double>(r2+1, c1)
                           + integralSqImg.at<double>(r1, c1)) / cnt;
            double stddev = std::sqrt(std::max(0.0, sqMean - mean * mean));
            thresh.at<double>(r, c) = mean * (1.0 + k * (stddev / R - 1.0));
        }
    }
    return thresh;
}

cv::Mat thresholdDarkAreas(const cv::Mat& img, double charLength) {
    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img.clone();
    }

    if (cv::mean(gray)[0] <= 127.0) {
        cv::bitwise_not(gray, gray);
    }

    int threshKernel = static_cast<int>(charLength) / 2 * 2 + 1;
    if (threshKernel < 3) threshKernel = 3;

    cv::Mat threshVal = sauvolaThreshold(gray, threshKernel, 0.2, 128.0);

    cv::Mat thresh(gray.rows, gray.cols, CV_8UC1, cv::Scalar(0));
    for (int r = 0; r < gray.rows; r++) {
        for (int c = 0; c < gray.cols; c++) {
            if (gray.at<uchar>(r, c) <= static_cast<uchar>(threshVal.at<double>(r, c)))
                thresh.at<uchar>(r, c) = 255;
        }
    }

    int blurSize = std::min(255, static_cast<int>(2 * charLength) / 2 * 2 + 1);
    if (blurSize % 2 == 0) blurSize++;
    if (blurSize < 3) blurSize = 3;
    cv::Mat blurImg;
    cv::GaussianBlur(gray, blurImg, cv::Size(blurSize, blurSize), 0);
    cv::Mat mask;
    cv::inRange(blurImg, cv::Scalar(0), cv::Scalar(100), mask);

    cv::Mat labels, stats, centroids;
    int nLabels = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);

    for (int idx = 1; idx < nLabels; idx++) {
        int x = stats.at<int>(idx, cv::CC_STAT_LEFT);
        int y = stats.at<int>(idx, cv::CC_STAT_TOP);
        int w = stats.at<int>(idx, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(idx, cv::CC_STAT_HEIGHT);
        int area = stats.at<int>(idx, cv::CC_STAT_AREA);

        if (w <= 0 || h <= 0) continue;
        if (static_cast<double>(area) / (w * h) >= 0.5 &&
            std::min(w, h) >= charLength &&
            std::max(w, h) >= 5 * charLength) {

            int mL = std::min(x, threshKernel);
            int mT = std::min(y, threshKernel);
            int mR = std::min(gray.cols - (x + w), threshKernel);
            int mB = std::min(gray.rows - (y + h), threshKernel);

            int rx = x - mL;
            int ry = y - mT;
            int rw = w + mL + mR;
            int rh = h + mT + mB;
            rx = std::max(0, rx);
            ry = std::max(0, ry);
            rw = std::min(rw, gray.cols - rx);
            rh = std::min(rh, gray.rows - ry);
            if (rw <= 0 || rh <= 0) continue;

            cv::Mat roi = gray(cv::Rect(rx, ry, rw, rh)).clone();
            cv::bitwise_not(roi, roi);

            cv::Mat roiThreshVal = sauvolaThreshold(roi, threshKernel, 0.2, 128.0);
            cv::Mat roiBinary(roi.rows, roi.cols, CV_8UC1, cv::Scalar(0));
            for (int rr = 0; rr < roi.rows; rr++) {
                for (int cc = 0; cc < roi.cols; cc++) {
                    if (roi.at<uchar>(rr, cc) <= static_cast<uchar>(roiThreshVal.at<double>(rr, cc)))
                        roiBinary.at<uchar>(rr, cc) = 255;
                }
            }

            int srcX = mL;
            int srcY = mT;
            if (srcX >= 0 && srcY >= 0 && srcX + w <= roiBinary.cols && srcY + h <= roiBinary.rows) {
                roiBinary(cv::Rect(srcX, srcY, w, h)).copyTo(
                    thresh(cv::Rect(x, y, w, h)));
            }
        }
    }

    return thresh;
}

} // namespace table
