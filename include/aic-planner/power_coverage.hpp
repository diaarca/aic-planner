#pragma once

#include "production_network.hpp"

namespace aic
{
bool place_electric_pylons(BlueprintLayout &layout, const FactoryNetwork &network,
                           const Facility &pylon, int grid_width, int grid_height,
                           const std::vector<std::pair<int, int>> &reserved_cells);
} // namespace aic
