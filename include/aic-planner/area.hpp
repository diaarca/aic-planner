#pragma once

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace aic
{
struct Facility;

struct StaticDepot
{
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

struct AreaSource
{
    std::string item;
    std::string facility;
    int x = 0;
    int y = 0;
    int rotation = 0;
    double limit = 0; // per minute
    std::string conduit_connection;
};

struct DepotBusPiece
{
    std::string facility;
    int x = 0;
    int y = 0;
    int rotation = 0;
    int width = 0;
    int height = 0;
};

struct AreaClimate
{
    int environment = 0;
    std::string gas;
    double limit = 0; // Feed per disperser per minute.
    int range_x = 0;
    int range_y = 0;
};

struct ArrayProjector
{
    std::string mode;
    int dx = 0;
    int dy = 0;
    int rotation = 0;
    double feed = 0; // Xiragen per minute.
};

struct ArrayField
{
    int belt_rotation = 0;
    double cycle_seconds = 0;
    std::vector<ArrayProjector> projectors;
};

struct Area
{
    std::string name;
    double pac_width = 0;
    double pac_height = 0;
    double pac_depot_width = 0;
    double pac_depot_height = 0;
    std::map<std::string, double> area_facilities;
    std::vector<StaticDepot> static_depots;
    std::vector<AreaSource> sources;
    std::vector<DepotBusPiece> depot_bus;
    std::map<int, AreaClimate> climates;
    std::map<std::string, ArrayField> array_fields;
    std::optional<DepotBusPiece> rebuilding_port;
    std::string power_pylon;
    bool output_to_depot_bus = false;
    bool allow_pipe_over_belt = false;

    static std::vector<Area> read_json(const std::string &filename);

    friend std::ostream &operator<<(std::ostream &os, const Area &a);
    static void print_table(const std::vector<Area> &areas);
};

void validate_area_installations(Area &area, const std::vector<Facility> &facilities);
std::vector<std::tuple<int, int, int>> depot_attachments(const Area &area, int width);

} // namespace aic
