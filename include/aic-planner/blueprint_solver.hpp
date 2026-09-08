#pragma once

#include "area.hpp"
#include "facility.hpp"
#include "mineral.hpp"
#include "product.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

struct PlacedPort
{
    int x = 0;
    int y = 0;
    bool is_input = false;
    int fx = 0;
    int fy = 0;
    std::string type;
    std::string item;
};

struct PlacedBlueprintEntity
{
    std::string type;
    std::string name;
    std::string code;
    int color_code = 39;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int rotation = 0; // 0, 90, 180, 270
    bool is_unloader = false;
    std::string mineral;
    std::vector<PlacedPort> ports;
};

enum ConveyorDir
{
    CONV_NORTH = 1,
    CONV_SOUTH = 2,
    CONV_EAST = 4,
    CONV_WEST = 8
};

struct BlueprintLayout
{
    std::string product_name;
    int bounding_width = 0;
    int bounding_height = 0;
    int depot_length = 0;
    std::vector<PlacedBlueprintEntity> entities;
    std::map<std::pair<int, int>, int> conveyor_cells; // (x, y) -> bitmask of ConveyorDir
    std::map<std::pair<int, int>, std::string> conveyor_items;
    bool is_conveyor_feasible = true;

    void print() const;
};

int getFacilityColor(const std::string& type);
int getItemColor(const std::string& item);

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
