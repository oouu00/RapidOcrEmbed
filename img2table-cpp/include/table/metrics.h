#pragma once
#include <opencv2/core.hpp>
#include "types.h"

namespace table {

bool computeImgMetrics(const cv::Mat& thresh, ImgMetrics& metrics);

} // namespace table
