#pragma once
#include "types.h"

namespace table {

Table clusterToTable(const std::vector<Cell>& clusterCells,
                     const std::vector<Cell>& elements);

} // namespace table
