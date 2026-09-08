#pragma once

#include "area.hpp"
#include "facility.hpp"
#include "mineral.hpp"
#include "product.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

struct PlacedBlueprintEntity
{
    std::string type;
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int rotation = 0; // 0, 90, 180, 270
    bool is_unloader = false;
    std::string mineral;
};

struct BlueprintLayout
{
    std::string product_name;
    int bounding_width = 0;
    int bounding_height = 0;
    int depot_length = 0;
    std::vector<PlacedBlueprintEntity> entities;

    void print() const;
};

class BlueprintSolver
{
  public:
    BlueprintSolver(const std::vector<Facility>& facilities,
                    const std::vector<Mineral>& minerals,
                    const std::vector<Area>& areas);

    bool solve(Product& product, BlueprintLayout& layout);

  private:
    const std::vector<Facility>& _facilities;
    const std::vector<Mineral>& _minerals;
    const std::vector<Area>& _areas;

    const Recipe* findRecipeFor(const std::string& item,
                                const Facility*& out_facility) const;
    bool isMineral(const std::string& item) const;
};
