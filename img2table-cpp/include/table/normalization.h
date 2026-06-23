#pragma once
#include "types.h"

namespace table {

std::vector<Cell> normalizeTableCells(const std::vector<Cell>& clusterCells,
                                      double charLength);
std::vector<int> clusterValues(const std::vector<double>& values,
                               double medianGapMultiple, double minGap);

} // namespace table
