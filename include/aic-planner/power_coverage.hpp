#pragma once

#include "production_network.hpp"
#include <chrono>

namespace aic
{
bool place_electric_pylons(
    BlueprintLayout &layout, const FactoryNetwork &network, const Facility &pylon, int grid_width,
    int grid_height, const std::vector<std::pair<int, int>> &reserved_cells,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
} // namespace aic
