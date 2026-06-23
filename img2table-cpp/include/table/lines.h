#pragma once
#include <opencv2/core.hpp>
#include "types.h"

namespace table {

void detectLines(const cv::Mat& img, const std::vector<Cell>& contours,
                 double charLength, int minLineLength,
                 std::vector<Line>& hLines, std::vector<Line>& vLines);

} // namespace table
