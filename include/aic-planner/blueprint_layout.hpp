#pragma once

#include "facility.hpp"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aic
{

struct PlacedPort
{
    int x = 0;
    int y = 0;
    bool is_input = false;
    int fx = 0;
    int fy = 0;
    TransportMedium type = TransportMedium::Solid;
    std::string item;
};

struct PlacedBlueprintEntity
{
    std::string type;
    std::string name;
    std::string code;
    std::string recipe_name;
    std::string seed_loop;
    double utilization = 0;
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
    double output_per_minute = 0;
    std::vector<std::string> startup_items;
    std::map<std::string, int> startup_stock;
    int bounding_width = 0;
    int bounding_height = 0;
    int depot_length = 0;
    std::vector<PlacedBlueprintEntity> entities;
    std::map<std::pair<int, int>, int> conveyor_cells; // (x, y) -> bitmask of ConveyorDir
    std::map<std::pair<int, int>, std::string> conveyor_items;
    bool is_conveyor_feasible = false;

    void normalize();
    void print() const;
};

std::string get_facility_code(const std::string &type, int index);
int get_facility_color(const std::string &type);
int get_item_color(const std::string &item);

// Clockwise rotation in the planner's y-up tile coordinates.
std::pair<int, int> rotate_cell(int x, int y, int width, int height, int rotation);
PlacedPort rotate_port(const PlacedPort &port, int width, int height, int rotation);
PlacedBlueprintEntity rotate_entity(const PlacedBlueprintEntity &entity, int width, int height,
                                    int rotation);
int rotate_conveyor_mask(int mask, int rotation);

} // namespace aic
