#pragma once
#include <opencv2/core.hpp>
#include "types.h"

namespace table {

struct ExtractorResult {
    std::vector<Table> tables;
    std::string html;
};

ExtractorResult extractBorderedTables(const cv::Mat& img);

} // namespace table
