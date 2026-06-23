#pragma once
#include "types.h"

namespace table {

std::vector<Cell> getCellsFromLines(const std::vector<Line>& hLines,
                                    const std::vector<Line>& vLines);
std::vector<Cell> deduplicateCells(const std::vector<Cell>& cells);

} // namespace table
