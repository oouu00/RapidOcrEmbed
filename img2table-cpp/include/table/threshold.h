#pragma once
#include <opencv2/core.hpp>

namespace table {

cv::Mat thresholdDarkAreas(const cv::Mat& img, double charLength);

} // namespace table
