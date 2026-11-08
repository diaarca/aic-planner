#pragma once
#include "area.hpp"
#include "facility.hpp"
#include "fuel.hpp"
#include "product.hpp"
#include "region.hpp"
#include <filesystem>

namespace aic
{
struct Dataset
{
    Region region;
    bool automatic_recipes = false;
    std::vector<Product> products;
    std::vector<Area> areas;
    std::vector<Fuel> fuels;
    std::vector<Facility> facilities;
    static Dataset load(const std::filesystem::path &folder);
};
} // namespace aic
