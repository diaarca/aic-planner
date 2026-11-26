#include "enkad_export.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace aic
{
namespace
{
using nlohmann::json;

// EnKAD clipboard format v4 uses its own numeric IDs. These cover the Valley IV catalog.
const std::map<std::string, int> building_ids = {
    {"component_mc_1", 17},
    {"filling_powder_mc_1", 8},
    {"furnance_1", 3},
    {"grinder_1", 11},
    {"planter_1", 5},
    {"power_diffuser_1", 15},
    {"seedcollector_1", 29},
    {"shaper_1", 24},
    {"storager_1", 12},
    {"thickener_1", 10},
    {"tools_assebling_mc_1", 19},
    {"unloader_1", 7},
};
const std::map<std::string, int> recipe_ids = {
    {"component_glass_cmpt_1", 117},
    {"component_iron_cmpt_1", 40},
    {"component_iron_enr_cmpt_1", 42},
    {"filling_bottled_food_1_1", 107},
    {"filling_bottled_food_2_1", 28},
    {"filling_bottled_food_3_1", 59},
    {"filling_bottled_rec_hp_1_1", 102},
    {"filling_bottled_rec_hp_2_1", 55},
    {"filling_bottled_rec_hp_3_1", 52},
    {"furnance_crystal_shell_1", 93},
    {"furnance_iron_enr_1", 113},
    {"furnance_iron_nugget_1", 105},
    {"furnance_quartz_glass_1", 74},
    {"grinder_iron_powder_1", 8},
    {"grinder_originium_powder_1", 81},
    {"grinder_plant_moss_powder_1_1", 106},
    {"grinder_plant_moss_powder_2_1", 12},
    {"grinder_plant_moss_powder_3_1", 51},
    {"planter_plant_bbflower_1", 34},
    {"planter_plant_moss_1_1", 57},
    {"planter_plant_moss_2_1", 24},
    {"planter_plant_moss_3_1", 108},
    {"seedcollector_plant_bbflower_1", 25},
    {"seedcollector_plant_moss_1_1", 68},
    {"seedcollector_plant_moss_2_1", 39},
    {"seedcollector_plant_moss_3_1", 120},
    {"shaper_glass_bottle_1", 13},
    {"shaper_iron_bottle_1", 79},
    {"shaper_iron_enr_bottle_1", 89},
    {"thickener_iron_enr_powder_1", 110},
    {"thickener_originium_enr_powder_1", 121},
    {"thickener_plant_moss_enr_powder_1_1", 115},
    {"thickener_plant_moss_enr_powder_2_1", 94},
    {"tools_proc_battery_1_1", 90},
    {"tools_proc_battery_2_1", 71},
    {"tools_proc_battery_3_1", 35},
};
const std::map<std::string, int> resource_ids = {
    {"amethyst_ore", 16},
    {"ferrium_ore", 63},
    {"originium_ore", 53},
};
constexpr int building_type = 0;
constexpr int belt_type = 1;
constexpr int solid_belt_id = 33;
constexpr int root_ground_layer = 3;

int require_id(const std::map<std::string, int> &ids, const std::string &name, const char *kind)
{
    const auto found = ids.find(name);
    if (found == ids.end())
        throw std::invalid_argument(std::string("EnKAD has no mapped ") + kind + " ID for " + name);
    return found->second;
}

json entity(int type, int id, int x, int y, const char *kind, json data)
{
    return {{"type", type},
            {"entityId", id},
            {"x", x},
            {"y", y},
            {"entityData", {{"case", kind}, {"value", std::move(data)}}}};
}
} // namespace

json enkad_blueprint(const BlueprintLayout &layout, const std::vector<Facility> &facilities)
{
    if (!layout.is_conveyor_feasible)
        throw std::invalid_argument("EnKAD export requires a routed blueprint");

    auto entities = json::array();
    // EnKAD reflects the source catalog's x axis. The planner reflects its z axis into y.
    for (const auto &placed : layout.entities)
    {
        const auto facility =
            std::find_if(facilities.begin(), facilities.end(),
                         [&](const Facility &entry) { return entry.name == placed.type; });
        if (facility == facilities.end())
            throw std::invalid_argument("EnKAD export cannot find facility " + placed.type);
        auto recipes = json::array();
        if (!placed.recipe_name.empty())
        {
            const auto recipe =
                std::find_if(facility->recipes.begin(), facility->recipes.end(),
                             [&](const Recipe &entry) { return entry.name == placed.recipe_name; });
            if (recipe == facility->recipes.end())
                throw std::invalid_argument("EnKAD export cannot find recipe " +
                                            placed.recipe_name);
            recipes.push_back(require_id(recipe_ids, recipe->id, "recipe"));
        }
        auto ports = json::array();
        if (placed.is_unloader)
            ports.push_back({{"portIndex", 0},
                             {"itemId", require_id(resource_ids, placed.mineral, "resource")}});
        entities.push_back(entity(building_type, require_id(building_ids, facility->id, "building"),
                                  layout.bounding_width - placed.x - placed.width,
                                  layout.bounding_height - placed.y - placed.height, "building",
                                  {{"rotation", (360 - placed.rotation) % 360},
                                   {"portItems", std::move(ports)},
                                   {"recipeIds", std::move(recipes)}}));
    }

    std::set<std::pair<int, int>> belt_cells;
    for (const auto &path : layout.conveyor_paths)
    {
        const auto [origin_x, origin_y] = path.at(1); // First belt tile beyond the output port.
        auto points = json::array();
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (i > 0 && i + 1 < path.size() && !belt_cells.insert(path[i]).second)
                throw std::invalid_argument("EnKAD export requires belts without overpasses");
            const auto [x, y] = path[i];
            const int point_type = i == 0 ? 1 : (i + 1 == path.size() ? 2 : 0);
            points.push_back({{"x", origin_x - x}, {"y", origin_y - y}, {"type", point_type}});
        }
        entities.push_back(entity(belt_type, solid_belt_id, layout.bounding_width - 1 - origin_x,
                                  layout.bounding_height - 1 - origin_y, "belt",
                                  {{"points", std::move(points)}}));
    }
    return {{"version", 4},
            {"layers",
             json::array({{{"layerTag", root_ground_layer}, {"entities", std::move(entities)}}})}};
}

std::filesystem::path write_enkad_blueprint(const std::filesystem::path &directory,
                                            const BlueprintLayout &layout,
                                            const std::vector<Facility> &facilities)
{
    const auto filename = layout.product_name + ".json";
    if (std::filesystem::path(filename).filename() != filename)
        throw std::invalid_argument("Invalid target name for an export filename");
    const auto contents = enkad_blueprint(layout, facilities).dump();
    const auto destination = directory / filename;
    std::ofstream output(destination);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << contents << '\n';
    output.close();
    return destination;
}
} // namespace aic
