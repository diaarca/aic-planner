#pragma once

#include "production_network.hpp"

namespace aic
{
// Fixed Valley IV plant module: planter / picker / planter, with a pylon below.
inline constexpr int plant_module_width = 15;
inline constexpr int plant_module_height = 9;
std::map<int, int> add_plant_pylons(FactoryNetwork &network, const RecipeGraph &graph);
void fix_plant_paths(FactoryNetwork &network, const BlueprintLayout &layout, bool standalone);
} // namespace aic
