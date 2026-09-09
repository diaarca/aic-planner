#include "blueprint_solver.hpp"
#include "ortools/sat/cp_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>

using namespace operations_research;
using namespace operations_research::sat;

BlueprintSolver::BlueprintSolver(const std::vector<Facility>& facilities,
                                 const std::vector<Mineral>& minerals,
                                 const std::vector<Area>& areas)
    : _facilities(facilities), _minerals(minerals), _areas(areas)
{
}

const Recipe*
BlueprintSolver::findRecipeFor(const std::string& item,
                               const Facility*& out_facility) const
{
    for (const auto& f : _facilities)
    {
        for (const auto& r : f.recipes)
        {
            for (const auto& p : r.products)
            {
                if (p.item == item)
                {
                    out_facility = &f;
                    return &r;
                }
            }
        }
    }
    out_facility = nullptr;
    return nullptr;
}

bool BlueprintSolver::isMineral(const std::string& item) const
{
    for (const auto& m : _minerals)
    {
        if (m.name == item)
        {
            return true;
        }
    }
    const Facility* dummy = nullptr;
    return (findRecipeFor(item, dummy) == nullptr);
}

int getFacilityColor(const std::string& type)
{
    if (type == "unloader")
    {
        return 208; // Bright Orange
    }
    if (type == "refining_unit")
    {
        return 39; // Deep Cyan
    }
    if (type == "fitting_unit")
    {
        return 48; // Emerald / Mint Green
    }
    if (type == "shredding_unit")
    {
        return 177; // Purple
    }
    if (type == "packaging_unit")
    {
        return 33; // Dodger Blue
    }
    if (type == "protocol_stash")
    {
        return 220; // Bright Gold / Yellow
    }
    if (type == "splitter")
    {
        return 51; // Bright Cyan
    }
    if (type == "bridge")
    {
        return 201; // Bright Magenta / Hot Pink
    }
    if (type == "moulding_unit")
        return 205;
    if (type == "seed_picking_unit")
        return 118;
    if (type == "planting_unit")
        return 34;
    if (type == "filling_unit")
        return 209;
    if (type == "grinding_unit")
        return 69;
    if (type == "gearing_unit")
        return 136;

    size_t hash = std::hash<std::string>{}(type);
    return 30 + static_cast<int>(hash % 190);
}

int getItemColor(const std::string& item)
{
    if (item == "ferrium_ore")
        return 208; // Bright Orange
    if (item == "ferrium")
        return 214; // Amber / Ingot Gold
    if (item == "ferrium_part")
        return 48; // Emerald / Mint Green
    if (item == "originium_ore")
        return 39; // Deep Cyan
    if (item == "originium_powder")
        return 177; // Purple / Lavender
    if (item == "sc_valley_battery")
        return 220; // Bright Gold / Yellow
    if (item == "hc_valley_battery")
        return 226; // Vivid Yellow
    if (item == "lc_valley_battery")
        return 214; // Amber
    if (item == "amethyst_ore")
        return 141; // Amethyst Violet
    if (item == "amethyst_part")
        return 147; // Light Violet
    if (item == "amethyst_bottle")
        return 219; // Pink / Rose
    if (item == "steel_part")
        return 252; // Light Silver
    if (item == "origocrust")
        return 180; // Tan
    if (item.rfind("canned_citrome", 0) == 0)
        return 215; // Peach
    if (item.rfind("buck_capsule", 0) == 0)
        return 81; // Sky Blue

    // Fallback: deterministic bright 256 color palette
    size_t hash = std::hash<std::string>{}(item);
    static const int palette[] = {45, 51, 82, 118, 154, 190, 208, 214, 220, 226, 201, 165, 171};
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

static std::string getFacilityCode(const std::string& type,
                                   int instance_idx = 0)
{
    if (type == "unloader")
        return "U" + std::to_string(instance_idx + 1);
    if (type == "splitter")
        return "SP";
    if (type == "refining_unit")
        return "R" + std::to_string(instance_idx + 1);
    if (type == "fitting_unit")
        return "F" + std::to_string(instance_idx + 1);
    if (type == "shredding_unit")
        return "S" + std::to_string(instance_idx + 1);
    if (type == "packaging_unit")
        return "PK";
    if (type == "protocol_stash")
        return "PS";
    if (type == "moulding_unit")
        return "ML";
    if (type == "seed_picking_unit")
        return "Sd";
    if (type == "planting_unit")
        return "PL";
    if (type == "filling_unit")
        return "FL";
    if (type == "grinding_unit")
        return "GR";
    if (type == "gearing_unit")
        return "GE";

    if (type.size() >= 2)
        return type.substr(0, 2);
    return "XX";
}

void BlueprintLayout::print() const
{
    if (entities.empty())
        return;

    int min_x = 9999, max_x = -9999;
    int min_y = 9999, max_y = -9999;
    for (const auto& e : entities)
    {
        min_x = std::min(min_x, e.x);
        max_x = std::max(max_x, e.x + e.width - 1);
        min_y = std::min(min_y, e.y);
        max_y = std::max(max_y, e.y + e.height - 1);
    }
    for (const auto& [cell, mask] : conveyor_cells)
    {
        min_x = std::min(min_x, cell.first);
        max_x = std::max(max_x, cell.first);
        min_y = std::min(min_y, cell.second);
        max_y = std::max(max_y, cell.second);
    }

    std::cout << "\n\033[1;36m================================================="
                 "===============================\033[0m\n";
    std::cout << "\033[1;37m  BLUEPRINT 2D TERMINAL MAP: \033[1;33m"
              << product_name << "\033[0m\n";
    std::cout << "  Bounding Box: \033[1m" << bounding_width << " x "
              << bounding_height << "\033[0m (Area: \033[1;32m"
              << (bounding_width * bounding_height) << "\033[0m) | "
              << "Depot Length: \033[1;35m" << depot_length << "\033[0m\n";
    std::cout << "\033[1;36m==================================================="
                 "=============================\033[0m\n\n";

    struct CellInfo
    {
        int bg = -1;
        int fg = 16;
        std::string text = " ·";
    };
    std::map<std::pair<int, int>, CellInfo> grid;

    for (const auto& e : entities)
    {
        int cx = e.x + e.width / 2;
        int cy = e.y + e.height / 2;
        int fg =
            (e.color_code == 33 || e.color_code == 160 || e.color_code == 34)
                ? 15
                : 16;

        for (int dy = 0; dy < e.height; ++dy)
        {
            for (int dx = 0; dx < e.width; ++dx)
            {
                int cur_x = e.x + dx;
                int cur_y = e.y + dy;

                CellInfo ci;
                ci.bg = e.color_code;
                ci.fg = fg;
                ci.text = "  ";

                if (cur_x == cx && cur_y == cy)
                {
                    ci.text = e.code;
                    if (ci.text.size() == 1)
                        ci.text += " ";
                }

                for (const auto& p : e.ports)
                {
                    if (p.x == cur_x && p.y == cur_y)
                    {
                        if (e.type == "splitter")
                        {
                            ci.bg = e.color_code;
                            ci.fg = 16;
                            if (p.is_input)
                            {
                                if (p.fy == 1)
                                    ci.text = "▼ ";
                                else if (p.fy == -1)
                                    ci.text = "▲ ";
                                else if (p.fx == 1)
                                    ci.text = "◄ ";
                                else if (p.fx == -1)
                                    ci.text = "► ";
                                break;
                            }
                            continue;
                        }

                        if (!p.item.empty())
                        {
                            ci.fg = getItemColor(p.item);
                        }
                        if (p.is_input)
                        {
                            // Input arrow enters the facility
                            if (p.fy == 1)
                                ci.text = "▼ ";
                            else if (p.fy == -1)
                                ci.text = "▲ ";
                            else if (p.fx == 1)
                                ci.text = "◄ ";
                            else if (p.fx == -1)
                                ci.text = "► ";
                        }
                        else
                        {
                            // Output arrow exits the facility
                            if (p.fy == 1)
                                ci.text = "▲ ";
                            else if (p.fy == -1)
                                ci.text = "▼ ";
                            else if (p.fx == 1)
                                ci.text = "► ";
                            else if (p.fx == -1)
                                ci.text = "◄ ";
                        }
                        break;
                    }
                }
                grid[{cur_x, cur_y}] = ci;
            }
        }
    }

    // Place conveyors on empty cells
    for (const auto& [cell, mask] : conveyor_cells)
    {
        int cx = cell.first;
        int cy = cell.second;
        if (grid.count({cx, cy}))
            continue; // Entity footprint takes precedence

        CellInfo ci;
        ci.bg = -1;

        std::string item = "";
        auto it_item = conveyor_items.find(cell);
        if (it_item != conveyor_items.end())
        {
            item = it_item->second;
        }

        if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
            (mask & CONV_EAST) && (mask & CONV_WEST))
        {
            ci.fg = 201; // Bright Magenta / Hot Pink for 4-way bridge crossings
        }
        else if (!item.empty())
        {
            ci.fg = getItemColor(item);
        }
        else
        {
            ci.fg = 220; // Default gold/yellow
        }

        std::string sym = "══";
        if (mask == (CONV_NORTH | CONV_SOUTH))
            sym = "║ ";
        else if (mask == (CONV_EAST | CONV_WEST))
            sym = "══";
        else if (mask == (CONV_NORTH | CONV_EAST))
            sym = "╚═";
        else if (mask == (CONV_SOUTH | CONV_EAST))
            sym = "╔═";
        else if (mask == (CONV_NORTH | CONV_WEST))
            sym = "╝ ";
        else if (mask == (CONV_SOUTH | CONV_WEST))
            sym = "╗ ";
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                 (mask & CONV_EAST) && (mask & CONV_WEST))
            sym = "╬═";
        else if ((mask & CONV_EAST) && (mask & CONV_WEST) &&
                 (mask & CONV_NORTH))
            sym = "╩═";
        else if ((mask & CONV_EAST) && (mask & CONV_WEST) &&
                 (mask & CONV_SOUTH))
            sym = "╦═";
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                 (mask & CONV_EAST))
            sym = "╠═";
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                 (mask & CONV_WEST))
            sym = "╣ ";
        else if (mask & (CONV_NORTH | CONV_SOUTH))
            sym = "║ ";
        else if (mask & (CONV_EAST | CONV_WEST))
            sym = "══";

        ci.text = sym;
        grid[{cx, cy}] = ci;
    }

    // Top depot banner
    bool has_depot_at_top = false;
    for (const auto& e : entities)
    {
        if (e.is_unloader && e.y + e.height - 1 == max_y)
            has_depot_at_top = true;
    }
    if (has_depot_at_top)
    {
        std::cout << "        \033[1;33m";
        for (int x = min_x; x <= max_x; ++x)
            std::cout << "══";
        std::cout << " [STATIC DEPOT BOUNDARY]\033[0m\n";
    }

    // X-axis header
    if (max_x >= 10)
    {
        std::cout << "        ";
        for (int x = min_x; x <= max_x; ++x)
        {
            int t = x / 10;
            if (t > 0)
                std::cout << " " << t;
            else
                std::cout << "  ";
        }
        std::cout << "\n";
    }
    std::cout << "        ";
    for (int x = min_x; x <= max_x; ++x)
    {
        std::cout << " " << (x % 10);
    }
    std::cout << "\n";

    // Top border
    std::cout << "      ┌";
    for (int x = min_x; x <= max_x; ++x)
        std::cout << "──";
    std::cout << "┐\n";

    // Rows
    for (int y = max_y; y >= min_y; --y)
    {
        std::cout << "  " << std::setw(2) << y << "  │";
        for (int x = min_x; x <= max_x; ++x)
        {
            auto it = grid.find({x, y});
            if (it != grid.end())
            {
                if (it->second.bg >= 0)
                {
                    std::cout << "\033[48;5;" << it->second.bg << "m\033[38;5;"
                              << it->second.fg << "m" << it->second.text
                              << "\033[0m";
                }
                else
                {
                    // Conveyor tile with item color
                    std::cout << "\033[38;5;" << it->second.fg << "m"
                              << it->second.text << "\033[0m";
                }
            }
            else
            {
                std::cout << "\033[38;5;239m ·\033[0m";
            }
        }
        std::cout << "│  " << std::setw(2) << y << "\n";
    }

    // Bottom border
    std::cout << "      └";
    for (int x = min_x; x <= max_x; ++x)
        std::cout << "──";
    std::cout << "┘\n";

    // Bottom X-axis
    std::cout << "        ";
    for (int x = min_x; x <= max_x; ++x)
    {
        std::cout << " " << (x % 10);
    }
    std::cout << "\n";

    // Print Legend
    std::cout << "\033[1;36m---------------------------------------------------"
                 "-----------------------------\033[0m\n";
    std::cout << "\033[1;37m  FACILITY LEGEND\033[0m\n";
    std::cout << "\033[1;36m---------------------------------------------------"
                 "-----------------------------\033[0m\n";
    for (const auto& e : entities)
    {
        std::cout << "  \033[48;5;" << e.color_code << "m  \033[0m "
                  << "\033[1m[" << std::left << std::setw(2) << e.code
                  << "]\033[0m " << std::left << std::setw(26) << e.name
                  << " | Size: " << std::setw(4)
                  << (std::to_string(e.width) + "x" + std::to_string(e.height))
                  << " | Rot: " << std::right << std::setw(3) << e.rotation
                  << "°"
                  << " | Pos: (" << std::setw(2) << e.x << "," << std::setw(2)
                  << e.y << ")";
        if (e.is_unloader)
        {
            std::cout << " [Mineral: " << e.mineral << "]";
        }
        std::cout << "\n";
    }

    std::set<std::string> unique_items;
    for (const auto& [cell, item] : conveyor_items)
    {
        if (!item.empty())
            unique_items.insert(item);
    }
    if (!unique_items.empty())
    {
        std::cout << "  Conveyors: ";
        for (const auto& item : unique_items)
        {
            int color = getItemColor(item);
            std::cout << "\033[38;5;" << color << "m══ " << item
                      << "\033[0m   ";
        }
        std::cout << "\n";
    }
    std::cout << "  Turns: ╔═ ╗ ╚═ ╝ | Crossing (Bridge): \033[38;5;201m╬═\033[0m\n";
    std::cout << "  Port Arrows: ▲ ▼ ► ◄ (Output = exit facility, Input = enter facility, colored by item)\n";
    std::cout << "\033[1;36m==================================================="
                 "=============================\033[0m\n";
}

struct BlueprintEntityToPlace
{
    int id = 0;
    std::string type;
    std::string name;
    int base_w = 0;
    int base_h = 0;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    bool is_unloader = false;
    std::string item_produced;
    std::string item_consumed;
};

struct StreamLink
{
    int src_entity = 0;
    int src_port_idx = 0;
    int dst_entity = 0;
    int dst_port_idx = 0;
    std::string item;
    double rate = 0;
};

bool BlueprintSolver::solve(Product& product, BlueprintLayout& layout)
{
    const Facility* root_fac = nullptr;
    const Recipe* root_recipe = findRecipeFor(product.name, root_fac);
    if (!root_recipe || !root_fac)
    {
        return false;
    }

    double root_prod_amount = 1.0;
    for (const auto& p : root_recipe->products)
    {
        if (p.item == product.name)
        {
            root_prod_amount = p.amount;
            break;
        }
    }

    double root_time = root_recipe->time;
    double base_rate = root_prod_amount / root_time; // units per second

    product.production_time = root_time;
    product.mineral_consumption.clear();
    product.factory_facilities.clear();

    // Map of required item flow rates (items/s)
    std::map<std::string, double> item_flow_needed;
    for (const auto& ing : root_recipe->ingredients)
    {
        item_flow_needed[ing.item] += (ing.amount / root_time);
    }

    // Facilities needed count
    std::map<std::string, int> facility_counts;
    facility_counts[root_fac->name] += 1;

    // Mineral flow rates needed
    std::map<std::string, double> mineral_flows;

    // Intermediate facility instances
    struct IntermediateNode
    {
        const Facility* fac;
        const Recipe* recipe;
        int instance_count;
    };
    std::vector<IntermediateNode> intermediate_nodes;

    // Process upstream dependencies
    std::queue<std::pair<std::string, double>> q;
    for (const auto& [item, rate] : item_flow_needed)
    {
        q.push({item, rate});
    }

    while (!q.empty())
    {
        auto [item, rate] = q.front();
        q.pop();

        if (isMineral(item))
        {
            mineral_flows[item] += rate;
            continue;
        }

        const Facility* fac = nullptr;
        const Recipe* rec = findRecipeFor(item, fac);
        if (!rec || !fac)
        {
            mineral_flows[item] += rate;
            continue;
        }

        double out_amount = 1.0;
        for (const auto& p : rec->products)
        {
            if (p.item == item)
            {
                out_amount = p.amount;
                break;
            }
        }
        double single_rate = out_amount / rec->time;
        int count = static_cast<int>(std::ceil(rate / single_rate));

        facility_counts[fac->name] += count;
        intermediate_nodes.push_back({fac, rec, count});

        for (const auto& ing : rec->ingredients)
        {
            double ing_rate = (ing.amount / rec->time) * count;
            q.push({ing.item, ing_rate});
        }
    }

    // Mineral consumption per root batch
    for (const auto& [m, rate] : mineral_flows)
    {
        product.mineral_consumption[m] = rate * root_time;
    }

    // Count protocol stash
    facility_counts["protocol_stash"] += 1;
    for (const auto& [fname, cnt] : facility_counts)
    {
        product.factory_facilities[fname] = cnt;
    }

    // Build list of entities to place
    std::vector<BlueprintEntityToPlace> entities;
    int next_id = 0;

    // 1. Intermediate facilities
    std::map<std::string, std::vector<int>> fac_ids_by_produced_item;
    for (const auto& node : intermediate_nodes)
    {
        for (int c = 0; c < node.instance_count; ++c)
        {
            BlueprintEntityToPlace ent;
            ent.id = next_id++;
            ent.type = node.fac->name;
            ent.name = node.fac->name + "_" + std::to_string(c + 1);
            ent.base_w = node.fac->width;
            ent.base_h = node.fac->height;
            ent.inputs = node.fac->inputs;
            ent.outputs = node.fac->outputs;
            ent.item_produced = node.recipe->products[0].item;
            if (!node.recipe->ingredients.empty())
                ent.item_consumed = node.recipe->ingredients[0].item;

            fac_ids_by_produced_item[ent.item_produced].push_back(ent.id);
            entities.push_back(ent);
        }
    }

    // 2. Root facility
    int root_entity_id = next_id++;
    {
        BlueprintEntityToPlace ent;
        ent.id = root_entity_id;
        ent.type = root_fac->name;
        ent.name = root_fac->name + "_root";
        ent.base_w = root_fac->width;
        ent.base_h = root_fac->height;
        ent.inputs = root_fac->inputs;
        ent.outputs = root_fac->outputs;
        ent.item_produced = product.name;
        entities.push_back(ent);
    }

    // 3. Protocol Stash
    int stash_entity_id = next_id++;
    {
        BlueprintEntityToPlace ent;
        ent.id = stash_entity_id;
        ent.type = "protocol_stash";
        ent.name = "protocol_stash";
        ent.base_w = 3;
        ent.base_h = 3;
        ent.inputs.push_back({0, 0, "product", 0, -1});
        ent.inputs.push_back({1, 0, "product", 0, -1});
        ent.inputs.push_back({2, 0, "product", 0, -1});
        entities.push_back(ent);
    }

    // Streams to connect
    std::vector<StreamLink> streams;
    std::map<int, int> used_in_port;
    std::map<int, int> used_out_port;

    auto add_stream = [&](int src, int dst, const std::string& item, double rate) {
        int out_p = used_out_port[src]++;
        int in_p;
        // For 3x3 single-input facilities (refining, shredding, fitting), use center port (index 1)
        if (dst < static_cast<int>(entities.size()) && entities[dst].inputs.size() == 3 && used_in_port[dst] == 0)
        {
            in_p = 1;
            used_in_port[dst] = 2; // Reserve center port
        }
        else
        {
            in_p = used_in_port[dst]++;
        }
        streams.push_back({src, out_p, dst, in_p, item, rate});
    };

    // 4. Unloaders: Direct 1-to-1 unloader allocation to cap every facility at 100% efficiency
    // Each unloader provides 0.5 items/s onto its output conveyor belt.
    int total_unloaders = 0;
    std::vector<int> unloader_entity_ids;
    for (const auto& [m, total_rate] : mineral_flows)
    {
        std::vector<int> consumers;
        for (const auto& ent : entities)
        {
            if (ent.item_consumed == m && !ent.is_unloader && ent.type != "splitter")
            {
                consumers.push_back(ent.id);
            }
        }
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == m)
            {
                consumers.push_back(root_entity_id);
            }
        }

        for (int cid : consumers)
        {
            double consumer_demand = 0.5;
            if (cid == root_entity_id)
            {
                for (const auto& ing : root_recipe->ingredients)
                {
                    if (ing.item == m)
                        consumer_demand = ing.amount / root_time;
                }
            }
            else
            {
                for (const auto& node : intermediate_nodes)
                {
                    if (node.fac->name == entities[cid].type)
                    {
                        for (const auto& ing : node.recipe->ingredients)
                        {
                            if (ing.item == m)
                                consumer_demand = ing.amount / node.recipe->time;
                        }
                    }
                }
            }

            int needed_u = std::max(1, static_cast<int>(std::ceil(consumer_demand / 0.5 - 1e-6)));
            for (int u = 0; u < needed_u; ++u)
            {
                BlueprintEntityToPlace ent;
                ent.id = next_id++;
                ent.type = "unloader";
                ent.name = "unloader_" + m + "_" + std::to_string(total_unloaders + 1);
                ent.base_w = 3;
                ent.base_h = 1;
                ent.is_unloader = true;
                ent.item_produced = m;
                ent.outputs.push_back({1, 0, "product", 0, -1}); // center output facing South
                unloader_entity_ids.push_back(ent.id);
                entities.push_back(ent);
                total_unloaders++;

                add_stream(ent.id, cid, m, std::min(0.5, consumer_demand));
            }
        }
    }

    // 5. Connect intermediate facilities
    // Intermediate-to-intermediate (e.g. refining -> fitting)
    for (const auto& [item, f_ids] : fac_ids_by_produced_item)
    {
        bool consumed_by_root = false;
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == item)
            {
                consumed_by_root = true;
                break;
            }
        }
        if (!consumed_by_root)
        {
            std::vector<int> inter_consumers;
            for (const auto& ent : entities)
            {
                if (ent.item_consumed == item && ent.id != root_entity_id &&
                    !ent.is_unloader && ent.type != "splitter")
                {
                    inter_consumers.push_back(ent.id);
                }
            }
            if (f_ids.size() >= inter_consumers.size())
            {
                for (size_t f = 0; f < f_ids.size(); ++f)
                {
                    int cid = inter_consumers[f % inter_consumers.size()];
                    add_stream(f_ids[f], cid, item, 0.5);
                }
            }
            else
            {
                for (size_t c = 0; c < inter_consumers.size(); ++c)
                {
                    int fid = f_ids[c % f_ids.size()];
                    add_stream(fid, inter_consumers[c], item, 0.5);
                }
            }
        }
    }

    // Intermediate-to-root: symmetrically order flank vs center items
    std::vector<int> flank_fids;
    std::vector<int> center_fids;
    std::string flank_item, center_item;

    for (const auto& [item, f_ids] : fac_ids_by_produced_item)
    {
        bool consumed_by_root = false;
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == item) { consumed_by_root = true; break; }
        }
        if (consumed_by_root)
        {
            if (f_ids.size() == 2)
            {
                flank_fids = f_ids;
                flank_item = item;
            }
            else
            {
                center_fids = f_ids;
                center_item = item;
            }
        }
    }

    if (!flank_fids.empty() && !center_fids.empty())
    {
        // Flank 1 (left) to port 0
        int out_p0 = used_out_port[flank_fids[0]]++;
        streams.push_back({flank_fids[0], out_p0, root_entity_id, 0, flank_item, 0.5});

        // Center streams to ports 1, 2, 3...
        for (size_t c = 0; c < center_fids.size(); ++c)
        {
            int cout_p = used_out_port[center_fids[c]]++;
            int cin_p = static_cast<int>(c + 1);
            streams.push_back({center_fids[c], cout_p, root_entity_id, cin_p, center_item, 0.5});
        }

        // Flank 2 (right) to rightmost port (e.g. port 5 on 6-port packaging unit)
        int right_port = static_cast<int>(entities[root_entity_id].inputs.size()) - 1;
        int out_p1 = used_out_port[flank_fids[1]]++;
        streams.push_back({flank_fids[1], out_p1, root_entity_id, right_port, flank_item, 0.5});
    }
    else
    {
        for (const auto& [item, f_ids] : fac_ids_by_produced_item)
        {
            bool consumed_by_root = false;
            for (const auto& ing : root_recipe->ingredients)
            {
                if (ing.item == item) { consumed_by_root = true; break; }
            }
            if (consumed_by_root)
            {
                for (int fid : f_ids)
                {
                    add_stream(fid, root_entity_id, item, 0.5);
                }
            }
        }
    }

    // Root facility to stash
    add_stream(root_entity_id, stash_entity_id, product.name, base_rate);

    // Determine grid bounds from area
    int grid_w = 30;
    int grid_h = 30;
    if (!_areas.empty())
    {
        if (_areas[0].pac_width > 0)
            grid_w = static_cast<int>(_areas[0].pac_width);
        if (_areas[0].pac_height > 0)
            grid_h = static_cast<int>(_areas[0].pac_height);
    }

    // Build CP-SAT Model
    CpModelBuilder model;
    int N = entities.size();

    std::vector<IntVar> posX, posY, effW, effH;
    std::vector<std::vector<BoolVar>> rotB;
    std::vector<IntervalVar> intX, intY;

    for (int i = 0; i < N; ++i)
    {
        posX.push_back(model.NewIntVar(Domain(0, grid_w - 1))
                           .WithName(entities[i].name + "_x"));
        posY.push_back(model.NewIntVar(Domain(0, grid_h - 1))
                           .WithName(entities[i].name + "_y"));

        std::vector<BoolVar> b;
        for (int r = 0; r < 4; ++r)
        {
            b.push_back(model.NewBoolVar().WithName(entities[i].name + "_rot_" +
                                                    std::to_string(r)));
        }
        rotB.push_back(b);

        LinearExpr rot_sum;
        for (const auto& bv : b)
            rot_sum += bv;
        model.AddEquality(rot_sum, 1);

        int bw = entities[i].base_w;
        int bh = entities[i].base_h;

        // Unloaders attach to top depot: line Y = grid_h - 1, rotation 0
        if (entities[i].is_unloader)
        {
            model.AddEquality(b[0], 1);
            model.AddEquality(posY[i], grid_h - 1);
            model.AddLessOrEqual(posX[i], grid_w - bw);
        }

        IntVar w = model.NewIntVar(Domain(std::min(bw, bh), std::max(bw, bh)));
        IntVar h = model.NewIntVar(Domain(std::min(bw, bh), std::max(bw, bh)));

        LinearExpr expr_w =
            LinearExpr::Term(b[0], bw) + LinearExpr::Term(b[2], bw) +
            LinearExpr::Term(b[1], bh) + LinearExpr::Term(b[3], bh);
        LinearExpr expr_h =
            LinearExpr::Term(b[0], bh) + LinearExpr::Term(b[2], bh) +
            LinearExpr::Term(b[1], bw) + LinearExpr::Term(b[3], bw);

        model.AddEquality(w, expr_w);
        model.AddEquality(h, expr_h);

        effW.push_back(w);
        effH.push_back(h);

        IntVar end_x = model.NewIntVar(Domain(0, grid_w));
        IntVar end_y = model.NewIntVar(Domain(0, grid_h));
        model.AddEquality(end_x, posX[i] + w);
        model.AddEquality(end_y, posY[i] + h);

        intX.push_back(model.NewIntervalVar(posX[i], w, end_x));
        intY.push_back(model.NewIntervalVar(posY[i], h, end_y));
    }

    // Contiguous unloaders along top depot with flexible slot assignment
    std::vector<int> unloader_indices;
    for (int i = 0; i < N; ++i)
    {
        if (entities[i].is_unloader)
            unloader_indices.push_back(i);
    }
    int K = static_cast<int>(unloader_indices.size());
    if (K > 0)
    {
        std::vector<IntVar> unloader_slots;
        for (int k = 0; k < K; ++k)
        {
            unloader_slots.push_back(
                model.NewIntVar(Domain(0, K - 1)).WithName("u_slot_" + std::to_string(k)));
        }
        model.AddAllDifferent(unloader_slots);
        for (int k = 0; k < K; ++k)
        {
            int u_idx = unloader_indices[k];
            model.AddEquality(posX[u_idx], LinearExpr::Term(unloader_slots[k], 3));
        }
    }

    // Non-overlapping
    NoOverlap2DConstraint no_overlap = model.AddNoOverlap2D();
    for (size_t i = 0; i < intX.size(); ++i)
    {
        no_overlap.AddRectangle(intX[i], intY[i]);
    }

    // Topological Tier 1: Primary mineral consumers form a contiguous row under unloaders
    std::vector<int> primary_consumer_ids;
    int primary_total_w = 0;
    for (const auto& ent : entities)
    {
        if (!ent.is_unloader && ent.type != "protocol_stash" && ent.id != root_entity_id)
        {
            if (isMineral(ent.item_consumed))
            {
                primary_consumer_ids.push_back(ent.id);
                primary_total_w += ent.base_w;
            }
        }
    }

    if (!primary_consumer_ids.empty() && primary_total_w <= grid_w)
    {
        IntVar tier1_y = model.NewIntVar(Domain(0, grid_h - 1)).WithName("tier1_y");
        for (int cid : primary_consumer_ids)
        {
            model.AddEquality(posY[cid], tier1_y);
            model.AddEquality(rotB[cid][2], 1); // rot 180: inputs North (facing unloaders), outputs South
        }
        for (int i = 0; i < N; ++i)
        {
            if (!entities[i].is_unloader &&
                std::find(primary_consumer_ids.begin(), primary_consumer_ids.end(), i) ==
                    primary_consumer_ids.end())
            {
                model.AddLessOrEqual(posY[i] + effH[i], tier1_y);
            }
        }
    }

    // Root packaging unit: inputs face North toward intermediate converters, outputs face South
    if (entities[root_entity_id].type == "packaging_unit")
    {
        model.AddEquality(rotB[root_entity_id][2], 1); // rot 180: inputs North, outputs South
    }

    // Protocol stash is at or below root facility
    model.AddLessOrEqual(posY[stash_entity_id], posY[root_entity_id]);

    // Port Access Tiles & Clearance
    struct PortAccessVar
    {
        int entity_id;
        bool is_input;
        int port_idx;
        IntVar ax;
        IntVar ay;
    };
    std::vector<PortAccessVar> port_accesses;

    for (int i = 0; i < N; ++i)
    {
        const auto& ent = entities[i];
        int bw = ent.base_w;
        int bh = ent.base_h;
        const auto& b = rotB[i];

        auto add_ports = [&](const std::vector<Port>& ports, bool is_input)
        {
            for (size_t p = 0; p < ports.size(); ++p)
            {
                const auto& port = ports[p];
                LinearExpr px = LinearExpr::Term(b[0], port.x) +
                                LinearExpr::Term(b[1], port.y) +
                                LinearExpr::Term(b[2], bw - 1 - port.x) +
                                LinearExpr::Term(b[3], bh - 1 - port.y);
                LinearExpr py = LinearExpr::Term(b[0], port.y) +
                                LinearExpr::Term(b[1], bw - 1 - port.x) +
                                LinearExpr::Term(b[2], bh - 1 - port.y) +
                                LinearExpr::Term(b[3], port.x);

                LinearExpr fx = LinearExpr::Term(b[0], port.fx) +
                                LinearExpr::Term(b[1], port.fy) -
                                LinearExpr::Term(b[2], port.fx) -
                                LinearExpr::Term(b[3], port.fy);
                LinearExpr fy = LinearExpr::Term(b[0], port.fy) -
                                LinearExpr::Term(b[1], port.fx) -
                                LinearExpr::Term(b[2], port.fy) +
                                LinearExpr::Term(b[3], port.fx);

                IntVar ax = model.NewIntVar(Domain(0, grid_w - 1));
                IntVar ay = model.NewIntVar(Domain(0, grid_h - 1));
                model.AddEquality(ax, posX[i] + px + fx);
                model.AddEquality(ay, posY[i] + py + fy);

                // Port access cell must NOT be covered by any other entity j !=
                // i
                for (int j = 0; j < N; ++j)
                {
                    if (j == i)
                        continue;
                    BoolVar left = model.NewBoolVar();
                    BoolVar right = model.NewBoolVar();
                    BoolVar below = model.NewBoolVar();
                    BoolVar above = model.NewBoolVar();

                    model.AddLessOrEqual(ax, posX[j] - 1).OnlyEnforceIf(left);
                    model.AddGreaterOrEqual(ax, posX[j] + effW[j])
                        .OnlyEnforceIf(right);
                    model.AddLessOrEqual(ay, posY[j] - 1).OnlyEnforceIf(below);
                    model.AddGreaterOrEqual(ay, posY[j] + effH[j])
                        .OnlyEnforceIf(above);

                    model.AddBoolOr({left, right, below, above});
                }

                // For multi-input facilities (e.g. packaging_unit), also ensure the 2nd tile in front is clear
                if (is_input && ent.inputs.size() > 2)
                {
                    IntVar ax2 = model.NewIntVar(Domain(0, grid_w - 1));
                    IntVar ay2 = model.NewIntVar(Domain(0, grid_h - 1));
                    model.AddEquality(ax2, posX[i] + px + 2 * fx);
                    model.AddEquality(ay2, posY[i] + py + 2 * fy);

                    for (int j = 0; j < N; ++j)
                    {
                        if (j == i)
                            continue;
                        BoolVar left2 = model.NewBoolVar();
                        BoolVar right2 = model.NewBoolVar();
                        BoolVar below2 = model.NewBoolVar();
                        BoolVar above2 = model.NewBoolVar();

                        model.AddLessOrEqual(ax2, posX[j] - 1).OnlyEnforceIf(left2);
                        model.AddGreaterOrEqual(ax2, posX[j] + effW[j])
                            .OnlyEnforceIf(right2);
                        model.AddLessOrEqual(ay2, posY[j] - 1).OnlyEnforceIf(below2);
                        model.AddGreaterOrEqual(ay2, posY[j] + effH[j])
                            .OnlyEnforceIf(above2);

                        model.AddBoolOr({left2, right2, below2, above2});
                    }
                }

                port_accesses.push_back(
                    {i, is_input, static_cast<int>(p), ax, ay});
            }
        };

        add_ports(ent.inputs, true);
        add_ports(ent.outputs, false);
    }

    // Bounding Box variables
    IntVar max_x = model.NewIntVar(Domain(0, grid_w)).WithName("max_x");
    IntVar max_y = model.NewIntVar(Domain(0, grid_h)).WithName("max_y");
    IntVar min_x = model.NewIntVar(Domain(0, grid_w)).WithName("min_x");
    IntVar min_y = model.NewIntVar(Domain(0, grid_h)).WithName("min_y");

    for (int i = 0; i < N; ++i)
    {
        model.AddGreaterOrEqual(max_x, posX[i] + effW[i]);
        model.AddGreaterOrEqual(max_y, posY[i] + effH[i]);
        model.AddLessOrEqual(min_x, posX[i]);
        model.AddLessOrEqual(min_y, posY[i]);
    }

    IntVar bbox_w = model.NewIntVar(Domain(1, grid_w)).WithName("bbox_w");
    IntVar bbox_h = model.NewIntVar(Domain(1, grid_h)).WithName("bbox_h");
    model.AddEquality(bbox_w, max_x - min_x);
    model.AddEquality(bbox_h, max_y - min_y);

    int total_fac_area = 0;
    for (const auto& ent : entities)
        total_fac_area += ent.base_w * ent.base_h;
    int max_dim = std::min(grid_w, std::max(total_unloaders * 3 + 8, static_cast<int>(std::ceil(std::sqrt(total_fac_area) * 2.3))));
    if (max_dim < grid_w)
    {
        model.AddLessOrEqual(bbox_w, max_dim);
        model.AddLessOrEqual(bbox_h, max_dim);
    }

    // Minimize Manhattan conveyor distances as regularization
    auto find_access = [&](int ent_id, bool is_in,
                           int p_idx) -> const PortAccessVar*
    {
        for (const auto& pa : port_accesses)
        {
            if (pa.entity_id == ent_id && pa.is_input == is_in &&
                pa.port_idx == p_idx)
            {
                return &pa;
            }
        }
        return nullptr;
    };

    struct ActivePort
    {
        int stream_idx;
        IntVar ax;
        IntVar ay;
    };
    std::vector<ActivePort> active_ports;

    LinearExpr stream_dist;
    for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
    {
        const auto& s = streams[s_idx];
        const auto* src_pa = find_access(s.src_entity, false, s.src_port_idx);
        const auto* dst_pa = find_access(s.dst_entity, true, s.dst_port_idx);
        if (src_pa && dst_pa)
        {
            IntVar dx = model.NewIntVar(Domain(0, grid_w));
            IntVar dy = model.NewIntVar(Domain(0, grid_h));
            model.AddAbsEquality(dx, src_pa->ax - dst_pa->ax);
            model.AddAbsEquality(dy, src_pa->ay - dst_pa->ay);
            stream_dist += dx + dy;

            active_ports.push_back({static_cast<int>(s_idx), src_pa->ax, src_pa->ay});
            active_ports.push_back({static_cast<int>(s_idx), dst_pa->ax, dst_pa->ay});
        }
    }

    // Active ports of distinct streams must not occupy the exact same access tile
    for (size_t i = 0; i < active_ports.size(); ++i)
    {
        for (size_t j = i + 1; j < active_ports.size(); ++j)
        {
            if (active_ports[i].stream_idx != active_ports[j].stream_idx)
            {
                BoolVar diff_x = model.NewBoolVar();
                BoolVar diff_y = model.NewBoolVar();
                model.AddNotEqual(active_ports[i].ax, active_ports[j].ax).OnlyEnforceIf(diff_x);
                model.AddNotEqual(active_ports[i].ay, active_ports[j].ay).OnlyEnforceIf(diff_y);
                model.AddBoolOr({diff_x, diff_y});
            }
        }
    }

    // Objective: prioritize compact bounding box and wirelength
    model.Minimize(bbox_w * 100 + bbox_h * 100 + stream_dist * 5);

    SatParameters params;
    params.set_max_time_in_seconds(2.0);
    params.set_num_search_workers(8);
    params.set_random_seed(42);

    const int max_conveyor_attempts = 50;

    for (int attempt = 1; attempt <= max_conveyor_attempts; ++attempt)
    {
        params.set_random_seed(42 + attempt * 17);
        Model cp_model;
        cp_model.Add(NewSatParameters(params));
        CpSolverResponse response = SolveCpModel(model.Build(), &cp_model);

        if (response.status() != CpSolverStatus::OPTIMAL &&
            response.status() != CpSolverStatus::FEASIBLE)
        {
            std::cerr << "[INFO] No more placements available in CP-SAT for "
                      << product.name << " after attempt " << (attempt - 1)
                      << std::endl;
            return false;
        }

        layout.entities.clear();
        std::map<std::string, int> type_instance_count;
        for (int i = 0; i < N; ++i)
        {
            PlacedBlueprintEntity pe;
            pe.name = entities[i].name;
            pe.type = entities[i].type;
            pe.x = SolutionIntegerValue(response, posX[i]);
            pe.y = SolutionIntegerValue(response, posY[i]);
            pe.width = SolutionIntegerValue(response, effW[i]);
            pe.height = SolutionIntegerValue(response, effH[i]);
            pe.is_unloader = entities[i].is_unloader;
            pe.mineral = entities[i].item_produced;

            int inst_idx = type_instance_count[pe.type]++;
            pe.code = getFacilityCode(pe.type, inst_idx);
            pe.color_code = getFacilityColor(pe.type);

            int rot = 0;
            for (int r = 0; r < 4; ++r)
            {
                if (SolutionBooleanValue(response, rotB[i][r]))
                {
                    rot = r * 90;
                    break;
                }
            }
            pe.rotation = rot;

            int bw = entities[i].base_w;
            int bh = entities[i].base_h;
            auto add_placed_port = [&](const Port& p, bool is_in)
            {
                int px = 0, py = 0, fx = 0, fy = 0;
                if (rot == 0)
                {
                    px = p.x;
                    py = p.y;
                    fx = p.fx;
                    fy = p.fy;
                }
                else if (rot == 90)
                {
                    px = p.y;
                    py = bw - 1 - p.x;
                    fx = p.fy;
                    fy = -p.fx;
                }
                else if (rot == 180)
                {
                    px = bw - 1 - p.x;
                    py = bh - 1 - p.y;
                    fx = -p.fx;
                    fy = -p.fy;
                }
                else if (rot == 270)
                {
                    px = bh - 1 - p.y;
                    py = p.x;
                    fx = -p.fy;
                    fy = p.fx;
                }
                pe.ports.push_back(
                    {pe.x + px, pe.y + py, is_in, fx, fy, p.type, ""});
            };

            for (const auto& in_p : entities[i].inputs)
                add_placed_port(in_p, true);
            for (const auto& out_p : entities[i].outputs)
                add_placed_port(out_p, false);

            layout.entities.push_back(pe);
        }

        // -------------------------------------------------------------
        // PathFinder Negotiated Congestion Conveyor Router
        // -------------------------------------------------------------
        layout.conveyor_cells.clear();
        layout.conveyor_items.clear();
        layout.is_conveyor_feasible = true;

        std::set<std::pair<int, int>> facility_cells;
        for (const auto& pe : layout.entities)
        {
            for (int dy = 0; dy < pe.height; ++dy)
            {
                for (int dx = 0; dx < pe.width; ++dx)
                {
                    facility_cells.insert({pe.x + dx, pe.y + dy});
                }
            }
        }

        auto get_dir_mask = [](int dx, int dy) -> int
        {
            if (dy == 1)
                return CONV_NORTH;
            if (dy == -1)
                return CONV_SOUTH;
            if (dx == 1)
                return CONV_EAST;
            if (dx == -1)
                return CONV_WEST;
            return 0;
        };

        auto get_dir_from_delta = [](int dx, int dy) -> int
        {
            if (dy == 1)
                return 0; // North
            if (dy == -1)
                return 1; // South
            if (dx == 1)
                return 2; // East
            if (dx == -1)
                return 3; // West
            return -1;
        };

        struct StreamEndpoints
        {
            PlacedPort src_port;
            PlacedPort dst_port;
            std::pair<int, int> start_pt;
            std::pair<int, int> target_pt;
            int dir_from_src_port;
            int dir_to_dst_port;
            bool valid;
        };

        std::vector<StreamEndpoints> stream_endpoints(streams.size());
        std::set<std::pair<int, int>> all_port_access_cells;

        // Planar Pin Matching / Port Order Normalization (Recommendation 3):
        // Dynamically sort input ports along their primary axis (X or Y) and
        // match them to incoming streams sorted by their source entity positions.
        std::map<int, std::vector<size_t>> streams_by_dst;
        for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
        {
            streams_by_dst[streams[s_idx].dst_entity].push_back(s_idx);
        }

        for (auto& [dst_id, s_indices] : streams_by_dst)
        {
            if (dst_id < 0 || dst_id >= static_cast<int>(layout.entities.size()) || s_indices.size() <= 1)
                continue;

            const auto& pe_dst = layout.entities[dst_id];
            std::vector<std::pair<PlacedPort, size_t>> input_ports_with_idx;
            for (size_t p = 0; p < pe_dst.ports.size(); ++p)
            {
                if (pe_dst.ports[p].is_input)
                    input_ports_with_idx.push_back({pe_dst.ports[p], p});
            }
            if (input_ports_with_idx.size() < s_indices.size())
                continue;

            // Determine primary axis of input port bank (horizontal or vertical)
            bool is_horizontal = true;
            if (input_ports_with_idx.size() >= 2)
            {
                is_horizontal = (std::abs(input_ports_with_idx.back().first.x - input_ports_with_idx.front().first.x) >=
                                 std::abs(input_ports_with_idx.back().first.y - input_ports_with_idx.front().first.y));
            }

            // Sort input ports along the axis
            std::sort(input_ports_with_idx.begin(), input_ports_with_idx.end(),
                      [&](const auto& a, const auto& b) {
                          return is_horizontal ? (a.first.x < b.first.x) : (a.first.y < b.first.y);
                      });

            // Sort incoming streams along the same axis based on source machine center
            std::sort(s_indices.begin(), s_indices.end(), [&](size_t a, size_t b) {
                const auto& src_a = layout.entities[streams[a].src_entity];
                const auto& src_b = layout.entities[streams[b].src_entity];
                return is_horizontal ? (src_a.x < src_b.x) : (src_a.y < src_b.y);
            });

            // Assign destination port index directly to match physical ordering
            for (size_t k = 0; k < s_indices.size(); ++k)
            {
                streams[s_indices[k]].dst_port_idx = static_cast<int>(input_ports_with_idx[k].second);
            }
        }

        for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
        {
            const auto& s = streams[s_idx];
            if (s.src_entity >= static_cast<int>(layout.entities.size()) ||
                s.dst_entity >= static_cast<int>(layout.entities.size()))
            {
                stream_endpoints[s_idx].valid = false;
                continue;
            }

            const auto& pe_src = layout.entities[s.src_entity];
            std::vector<PlacedPort> src_outputs;
            for (const auto& p : pe_src.ports)
            {
                if (!p.is_input)
                    src_outputs.push_back(p);
            }
            if (src_outputs.empty())
            {
                stream_endpoints[s_idx].valid = false;
                continue;
            }
            const auto& src_port =
                src_outputs[s.src_port_idx % src_outputs.size()];

            const auto& pe_dst = layout.entities[s.dst_entity];
            std::vector<PlacedPort> dst_inputs;
            for (const auto& p : pe_dst.ports)
            {
                if (p.is_input)
                    dst_inputs.push_back(p);
            }
            if (dst_inputs.empty())
            {
                stream_endpoints[s_idx].valid = false;
                continue;
            }
            const auto& dst_port =
                dst_inputs[s.dst_port_idx % dst_inputs.size()];

            int src_ax = src_port.x + src_port.fx;
            int src_ay = src_port.y + src_port.fy;
            int dst_ax = dst_port.x + dst_port.fx;
            int dst_ay = dst_port.y + dst_port.fy;

            int d_from_src = get_dir_from_delta(src_port.fx, src_port.fy);
            int d_to_dst = get_dir_from_delta(-dst_port.fx, -dst_port.fy);

            stream_endpoints[s_idx] = {src_port,
                                       dst_port,
                                       {src_ax, src_ay},
                                       {dst_ax, dst_ay},
                                       d_from_src,
                                       d_to_dst,
                                       true};
            all_port_access_cells.insert({src_ax, src_ay});
            all_port_access_cells.insert({dst_ax, dst_ay});
        }

        struct PathNode
        {
            int x, y, layer; // 0: Ground, 1: Overpass Bridge
        };

        struct OccSegment
        {
            int stream_idx;
            int dir_in;
            int dir_out;
            bool is_straight;
        };

        const int W = grid_w;
        const int H = grid_h;
        // cell_occ[W][H][2]: layer 0 = Ground, layer 1 = Bridge
        std::vector<std::vector<std::vector<std::vector<OccSegment>>>> cell_occ(
            W, std::vector<std::vector<std::vector<OccSegment>>>(
                   H, std::vector<std::vector<OccSegment>>(2)));
        std::vector<std::vector<int>> cell_history_penalty(
            W, std::vector<int>(H, 0));

        auto is_conflicted = [&](int x, int y) -> bool
        {
            const auto& occ0 = cell_occ[x][y][0];
            const auto& occ1 = cell_occ[x][y][1];
            if (occ0.size() > 1 || occ1.size() > 1)
                return true;
            if (occ0.empty() || occ1.empty())
                return false;
            // Both layers have 1 stream: valid if and only if both are straight and perpendicular
            if (!occ0[0].is_straight || !occ1[0].is_straight)
                return true;
            bool v0 = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
            bool v1 = (occ1[0].dir_in == 0 || occ1[0].dir_in == 1);
            return (v0 == v1); // If same orientation, conflicted! If different, valid perpendicular bridge
        };

        std::vector<std::vector<PathNode>> stream_paths(streams.size());

        auto add_path_to_occ = [&](int s_idx)
        {
            const auto& path = stream_paths[s_idx];
            if (path.empty())
                return;
            int m = static_cast<int>(path.size()) - 1;
            int d_from_src = stream_endpoints[s_idx].dir_from_src_port;
            int d_to_dst = stream_endpoints[s_idx].dir_to_dst_port;

            for (int k = 0; k <= m; ++k)
            {
                int d_in = (k == 0) ? d_from_src
                                    : get_dir_from_delta(
                                          path[k].x - path[k - 1].x,
                                          path[k].y - path[k - 1].y);
                int d_out = (k == m) ? d_to_dst
                                     : get_dir_from_delta(
                                           path[k + 1].x - path[k].x,
                                           path[k + 1].y - path[k].y);
                bool straight = (d_in == d_out);
                cell_occ[path[k].x][path[k].y][path[k].layer].push_back(
                    {s_idx, d_in, d_out, straight});
            }
        };

        auto remove_path_from_occ = [&](int s_idx)
        {
            const auto& path = stream_paths[s_idx];
            for (const auto& node : path)
            {
                auto& occ = cell_occ[node.x][node.y][node.layer];
                occ.erase(std::remove_if(occ.begin(), occ.end(),
                                         [&](const OccSegment& seg) {
                                             return seg.stream_idx == s_idx;
                                         }),
                          occ.end());
            }
        };

        auto route_stream = [&](int s_idx, double p_k) -> bool
        {
            const auto& ep = stream_endpoints[s_idx];
            if (!ep.valid)
                return false;

            std::pair<int, int> start_pt = ep.start_pt;
            std::pair<int, int> target_pt = ep.target_pt;
            int d_from_src = ep.dir_from_src_port;
            int d_to_dst = ep.dir_to_dst_port;

            if (start_pt == target_pt)
            {
                stream_paths[s_idx] = {{start_pt.first, start_pt.second, 0}};
                add_path_to_occ(s_idx);
                return true;
            }

            struct QNode
            {
                int cost;
                int x, y, layer, dir;
                bool operator>(const QNode& other) const
                {
                    return cost > other.cost;
                }
            };

            const int INF = 1e9;
            // dist[W][H][2][4]
            std::vector<std::vector<std::vector<std::vector<int>>>> dist(
                W, std::vector<std::vector<std::vector<int>>>(
                       H, std::vector<std::vector<int>>(
                              2, std::vector<int>(4, INF))));

            struct Parent
            {
                int x, y, layer, dir;
            };
            std::vector<std::vector<std::vector<std::vector<Parent>>>> parent(
                W, std::vector<std::vector<std::vector<Parent>>>(
                       H, std::vector<std::vector<Parent>>(
                              2, std::vector<Parent>(4, {-1, -1, -1, -1}))));

            std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> pq;

            dist[start_pt.first][start_pt.second][0][d_from_src] = 0;
            pq.push({0, start_pt.first, start_pt.second, 0, d_from_src});

            const int dxy[4][2] = {
                {0, 1}, {0, -1}, {1, 0}, {-1, 0}}; // 0: N, 1: S, 2: E, 3: W

            int best_target_cost = INF;
            int best_target_dir = -1;

            while (!pq.empty())
            {
                auto [c, x, y, layer, dir] = pq.top();
                pq.pop();

                if (c > dist[x][y][layer][dir])
                    continue;
                if (c >= best_target_cost)
                    break;

                // Docking at target: must be on Ground (layer 0)
                if (x == target_pt.first && y == target_pt.second && layer == 0)
                {
                    int turn_p = (dir != d_to_dst) ? 20 : 0;
                    int total_c = c + turn_p;
                    if (total_c < best_target_cost)
                    {
                        best_target_cost = total_c;
                        best_target_dir = dir;
                    }
                    continue;
                }

                if (layer == 0)
                {
                    // 1. Move on Ground (Layer 0)
                    for (int next_d = 0; next_d < 4; ++next_d)
                    {
                        if (next_d == (dir ^ 1))
                            continue; // Cannot 180 reverse

                        int nx = x + dxy[next_d][0];
                        int ny = y + dxy[next_d][1];

                        if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                            continue;
                        if (facility_cells.count({nx, ny}))
                            continue;

                        int turn_cost = (next_d != dir) ? 15 : 0;
                        int h_cost = cell_history_penalty[nx][ny];

                        int occ_cost = 0;
                        const auto& occ0 = cell_occ[nx][ny][0];
                        if (!occ0.empty())
                        {
                            occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                        }
                        if (all_port_access_cells.count({nx, ny}) &&
                            std::pair<int, int>{nx, ny} != target_pt)
                        {
                            occ_cost += 500;
                        }

                        int step_cost = 10;
                        int nc = c + step_cost + turn_cost + h_cost + occ_cost;
                        if (nc < dist[nx][ny][0][next_d])
                        {
                            dist[nx][ny][0][next_d] = nc;
                            parent[nx][ny][0][next_d] = {x, y, layer, dir};
                            pq.push({nc, nx, ny, 0, next_d});
                        }
                    }

                    // 2. Ramp Up to Overpass Bridge (Layer 1)
                    // Must continue straight ahead in current direction 'dir'
                    {
                        int nx = x + dxy[dir][0];
                        int ny = y + dxy[dir][1];

                        if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
                            !facility_cells.count({nx, ny}) &&
                            std::pair<int, int>{nx, ny} != target_pt)
                        {
                            int h_cost = cell_history_penalty[nx][ny];
                            int occ_cost = 0;
                            const auto& occ1 = cell_occ[nx][ny][1];
                            if (!occ1.empty())
                            {
                                occ_cost = static_cast<int>((1500.0 + 50.0 * h_cost) * p_k);
                            }
                            else
                            {
                                const auto& occ0 = cell_occ[nx][ny][0];
                                if (!occ0.empty())
                                {
                                    bool is_v = (dir == 0 || dir == 1);
                                    bool other_v = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
                                    if (occ0[0].is_straight && (is_v != other_v))
                                    {
                                        occ_cost = 0; // Valid perpendicular bridge over Layer 0!
                                    }
                                    else
                                    {
                                        occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                                    }
                                }
                            }

                            int bridge_step_cost = 30; // base (10) + bridge hardware (20)
                            int nc = c + bridge_step_cost + h_cost + occ_cost;
                            if (nc < dist[nx][ny][1][dir])
                            {
                                dist[nx][ny][1][dir] = nc;
                                parent[nx][ny][1][dir] = {x, y, layer, dir};
                                pq.push({nc, nx, ny, 1, dir});
                            }
                        }
                    }
                }
                else // layer == 1 (Currently on Overpass Bridge)
                {
                    // Physical rule: Bridges CANNOT turn! Must continue straight ahead in direction 'dir'.
                    int nx = x + dxy[dir][0];
                    int ny = y + dxy[dir][1];

                    if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
                        !facility_cells.count({nx, ny}))
                    {
                        int h_cost = cell_history_penalty[nx][ny];

                        // Choice A: Ramp Down to Ground (Layer 0)
                        {
                            int occ_cost = 0;
                            const auto& occ0 = cell_occ[nx][ny][0];
                            if (!occ0.empty())
                            {
                                occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                            }
                            if (all_port_access_cells.count({nx, ny}) &&
                                std::pair<int, int>{nx, ny} != target_pt)
                            {
                                occ_cost += 500;
                            }

                            int step_cost = 10;
                            int nc = c + step_cost + h_cost + occ_cost;
                            if (nc < dist[nx][ny][0][dir])
                            {
                                dist[nx][ny][0][dir] = nc;
                                parent[nx][ny][0][dir] = {x, y, layer, dir};
                                pq.push({nc, nx, ny, 0, dir});
                            }
                        }

                        // Choice B: Continue on Bridge (Layer 1) over another parallel belt
                        if (std::pair<int, int>{nx, ny} != target_pt)
                        {
                            int occ_cost = 0;
                            const auto& occ1 = cell_occ[nx][ny][1];
                            if (!occ1.empty())
                            {
                                occ_cost = static_cast<int>((1500.0 + 50.0 * h_cost) * p_k);
                            }
                            else
                            {
                                const auto& occ0 = cell_occ[nx][ny][0];
                                if (!occ0.empty())
                                {
                                    bool is_v = (dir == 0 || dir == 1);
                                    bool other_v = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
                                    if (occ0[0].is_straight && (is_v != other_v))
                                    {
                                        occ_cost = 0;
                                    }
                                    else
                                    {
                                        occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                                    }
                                }
                            }

                            int bridge_step_cost = 30;
                            int nc = c + bridge_step_cost + h_cost + occ_cost;
                            if (nc < dist[nx][ny][1][dir])
                            {
                                dist[nx][ny][1][dir] = nc;
                                parent[nx][ny][1][dir] = {x, y, layer, dir};
                                pq.push({nc, nx, ny, 1, dir});
                            }
                        }
                    }
                }
            }

            if (best_target_dir == -1)
            {
                return false;
            }

            std::vector<PathNode> path;
            int cx = target_pt.first;
            int cy = target_pt.second;
            int clayer = 0;
            int cdir = best_target_dir;

            while (true)
            {
                path.push_back({cx, cy, clayer});
                if (cx == start_pt.first && cy == start_pt.second &&
                    clayer == 0 && cdir == d_from_src)
                {
                    break;
                }
                Parent p = parent[cx][cy][clayer][cdir];
                cx = p.x;
                cy = p.y;
                clayer = p.layer;
                cdir = p.dir;
            }
            std::reverse(path.begin(), path.end());
            stream_paths[s_idx] = path;
            add_path_to_occ(s_idx);
            return true;
        };

        // Initial routing pass with soft congestion (p_0 = 1.0)
        double p_k = 1.0;
        bool all_initial_routed = true;
        for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
        {
            if (!route_stream(static_cast<int>(s_idx), p_k))
            {
                all_initial_routed = false;
            }
        }

        bool pf_feasible = false;
        std::vector<int> conflicted_stream_indices;
        std::set<std::pair<int, int>> final_conflicted_cells;

        const int max_pf_iters = 50;
        for (int iter = 0; iter < max_pf_iters; ++iter)
        {
            std::set<std::pair<int, int>> conflicted_cells;
            std::vector<int> cur_conflicted_streams;

            for (int y = 0; y < H; ++y)
            {
                for (int x = 0; x < W; ++x)
                {
                    if (is_conflicted(x, y))
                    {
                        conflicted_cells.insert({x, y});
                        for (const auto& occ : cell_occ[x][y][0])
                            cur_conflicted_streams.push_back(occ.stream_idx);
                        for (const auto& occ : cell_occ[x][y][1])
                            cur_conflicted_streams.push_back(occ.stream_idx);
                    }
                }
            }

            if (conflicted_cells.empty() && all_initial_routed)
            {
                pf_feasible = true;
                break;
            }


            std::sort(cur_conflicted_streams.begin(), cur_conflicted_streams.end());
            cur_conflicted_streams.erase(
                std::unique(cur_conflicted_streams.begin(), cur_conflicted_streams.end()),
                cur_conflicted_streams.end());

            conflicted_stream_indices = cur_conflicted_streams;
            final_conflicted_cells = conflicted_cells;

            // Increase historical penalty on conflicted cells
            for (const auto& [cx, cy] : conflicted_cells)
            {
                cell_history_penalty[cx][cy] += 35;
            }

            // Exponential VPR schedule for present congestion: p_{k+1} = min(25.0, 1.3 * p_k)
            p_k = std::min(25.0, p_k * 1.3);

            // Slack-based rip-up sorting (Recommendation 4):
            // On even iterations: sort ascending by slack (critical tight streams claim lane first).
            // On odd iterations: random shuffle to break cyclic locks.
            if (iter % 2 == 0)
            {
                std::sort(cur_conflicted_streams.begin(), cur_conflicted_streams.end(),
                          [&](int a, int b) {
                              int dist_a = std::abs(stream_endpoints[a].start_pt.first - stream_endpoints[a].target_pt.first) +
                                           std::abs(stream_endpoints[a].start_pt.second - stream_endpoints[a].target_pt.second);
                              int dist_b = std::abs(stream_endpoints[b].start_pt.first - stream_endpoints[b].target_pt.first) +
                                           std::abs(stream_endpoints[b].start_pt.second - stream_endpoints[b].target_pt.second);
                              int slack_a = static_cast<int>(stream_paths[a].size()) - dist_a;
                              int slack_b = static_cast<int>(stream_paths[b].size()) - dist_b;
                              return slack_a < slack_b;
                          });
            }
            else
            {
                std::mt19937 rng(42 + iter);
                std::shuffle(cur_conflicted_streams.begin(), cur_conflicted_streams.end(), rng);
            }

            // Synchronous rip-up: remove ALL conflicted streams FIRST
            for (int s_idx : cur_conflicted_streams)
            {
                remove_path_from_occ(s_idx);
            }

            // Immediately reroute each conflicted stream one by one
            bool reroute_all = true;
            for (int s_idx : cur_conflicted_streams)
            {
                if (!route_stream(s_idx, p_k))
                {
                    reroute_all = false;
                }
            }
            all_initial_routed = all_initial_routed && reroute_all;
        }

        if (pf_feasible)
        {
            // Assemble conveyor cells and items
            for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
            {
                const auto& s = streams[s_idx];
                const auto& path = stream_paths[s_idx];
                const auto& ep = stream_endpoints[s_idx];
                int m = static_cast<int>(path.size()) - 1;

                for (int k = 0; k <= m; ++k)
                {
                    auto pt = std::make_pair(path[k].x, path[k].y);
                    int mask = 0;
                    if (k == 0)
                    {
                        int to_src_dx = -ep.src_port.fx;
                        int to_src_dy = -ep.src_port.fy;
                        mask |= get_dir_mask(to_src_dx, to_src_dy);
                    }
                    else
                    {
                        int prev_dx = path[k - 1].x - pt.first;
                        int prev_dy = path[k - 1].y - pt.second;
                        mask |= get_dir_mask(prev_dx, prev_dy);
                    }

                    if (k == m)
                    {
                        int to_dst_dx = -ep.dst_port.fx;
                        int to_dst_dy = -ep.dst_port.fy;
                        mask |= get_dir_mask(to_dst_dx, to_dst_dy);
                    }
                    else
                    {
                        int next_dx = path[k + 1].x - pt.first;
                        int next_dy = path[k + 1].y - pt.second;
                        mask |= get_dir_mask(next_dx, next_dy);
                    }

                    layout.conveyor_cells[pt] |= mask;
                    layout.conveyor_items[pt] = s.item;
                }

                // Assign item to src and dst ports for color visualization
                for (auto& p : layout.entities[s.src_entity].ports)
                {
                    if (p.x == ep.src_port.x && p.y == ep.src_port.y)
                    {
                        p.item = s.item;
                        break;
                    }
                }
                for (auto& p : layout.entities[s.dst_entity].ports)
                {
                    if (p.x == ep.dst_port.x && p.y == ep.dst_port.y)
                    {
                        p.item = s.item;
                        break;
                    }
                }
            }

            if (attempt > 1)
            {
                std::cout << "[OK] PathFinder conveyor routing feasible for "
                          << product.name << " on attempt " << attempt << "!\n";
            }

            product.factory_width = SolutionIntegerValue(response, bbox_w);
            product.factory_height = SolutionIntegerValue(response, bbox_h);
            product.factory_depot = total_unloaders * 3;

            layout.product_name = product.name;
            layout.bounding_width = static_cast<int>(product.factory_width);
            layout.bounding_height = static_cast<int>(product.factory_height);
            layout.depot_length = static_cast<int>(product.factory_depot);

            // Coordinate Normalization
            int min_bx = 9999, max_bx = -9999;
            int min_by = 9999, max_by = -9999;
            for (const auto& pe : layout.entities)
            {
                min_bx = std::min(min_bx, pe.x);
                max_bx = std::max(max_bx, pe.x + pe.width - 1);
                min_by = std::min(min_by, pe.y);
                max_by = std::max(max_by, pe.y + pe.height - 1);
            }
            for (const auto& [cell, mask] : layout.conveyor_cells)
            {
                min_bx = std::min(min_bx, cell.first);
                max_bx = std::max(max_bx, cell.first);
                min_by = std::min(min_by, cell.second);
                max_by = std::max(max_by, cell.second);
            }

            for (auto& pe : layout.entities)
            {
                pe.x -= min_bx;
                pe.y -= min_by;
                for (auto& p : pe.ports)
                {
                    p.x -= min_bx;
                    p.y -= min_by;
                }
            }

            std::map<std::pair<int, int>, int> shifted_conveyors;
            std::map<std::pair<int, int>, std::string> shifted_items;
            for (const auto& [cell, mask] : layout.conveyor_cells)
            {
                shifted_conveyors[{cell.first - min_bx, cell.second - min_by}] =
                    mask;
            }
            for (const auto& [cell, item] : layout.conveyor_items)
            {
                shifted_items[{cell.first - min_bx, cell.second - min_by}] =
                    item;
            }
            layout.conveyor_cells = std::move(shifted_conveyors);
            layout.conveyor_items = std::move(shifted_items);

            layout.bounding_width = max_bx - min_bx + 1;
            layout.bounding_height = max_by - min_by + 1;
            product.factory_width = layout.bounding_width;
            product.factory_height = layout.bounding_height;

            return true;
        }

        // Conveyor routing infeasible: Add no-good cuts to CP-SAT
        std::cout << "[INFO] PathFinder conveyor check failed for "
                  << product.name << " on attempt " << attempt << " ("
                  << conflicted_stream_indices.size()
                  << " conflicted streams). Repeating subproblem with cuts...\n";

        std::vector<BoolVar> entity_matches(N);
        std::vector<BoolVar> not_matches;
        for (int i = 0; i < N; ++i)
        {
            int sol_x = SolutionIntegerValue(response, posX[i]);
            int sol_y = SolutionIntegerValue(response, posY[i]);
            int sol_r = 0;
            for (int r = 0; r < 4; ++r)
            {
                if (SolutionBooleanValue(response, rotB[i][r]))
                {
                    sol_r = r;
                    break;
                }
            }

            BoolVar match_x = model.NewBoolVar();
            BoolVar match_y = model.NewBoolVar();
            model.AddEquality(posX[i], sol_x).OnlyEnforceIf(match_x);
            model.AddNotEqual(posX[i], sol_x).OnlyEnforceIf(match_x.Not());
            model.AddEquality(posY[i], sol_y).OnlyEnforceIf(match_y);
            model.AddNotEqual(posY[i], sol_y).OnlyEnforceIf(match_y.Not());

            BoolVar match_i = model.NewBoolVar();
            model.AddBoolAnd({match_x, match_y, rotB[i][sol_r]})
                .OnlyEnforceIf(match_i);
            model.AddBoolOr(
                     {match_x.Not(), match_y.Not(), rotB[i][sol_r].Not()})
                .OnlyEnforceIf(match_i.Not());

            entity_matches[i] = match_i;
            not_matches.push_back(match_i.Not());
        }

        // Global cut: forbid the exact full placement
        model.AddBoolOr(not_matches);

        // Targeted cuts on conflicted streams (Recommendation 5: Relative displacement cuts)
        for (int s_idx : conflicted_stream_indices)
        {
            int u = streams[s_idx].src_entity;
            int v = streams[s_idx].dst_entity;
            if (u >= 0 && u < N && v >= 0 && v < N)
            {
                // Absolute placement cut
                model.AddBoolOr(
                    {entity_matches[u].Not(), entity_matches[v].Not()});

                // Relative displacement cut: forbid relative delta (dx, dy) and rotations
                int sol_xu = SolutionIntegerValue(response, posX[u]);
                int sol_yu = SolutionIntegerValue(response, posY[u]);
                int sol_xv = SolutionIntegerValue(response, posX[v]);
                int sol_yv = SolutionIntegerValue(response, posY[v]);
                int dx_star = sol_xu - sol_xv;
                int dy_star = sol_yu - sol_yv;

                int ru = 0, rv = 0;
                for (int r = 0; r < 4; ++r)
                {
                    if (SolutionBooleanValue(response, rotB[u][r]))
                        ru = r;
                    if (SolutionBooleanValue(response, rotB[v][r]))
                        rv = r;
                }

                LinearExpr diff_x = posX[u] - posX[v];
                LinearExpr diff_y = posY[u] - posY[v];
                BoolVar match_dx = model.NewBoolVar();
                BoolVar match_dy = model.NewBoolVar();
                model.AddEquality(diff_x, dx_star).OnlyEnforceIf(match_dx);
                model.AddNotEqual(diff_x, dx_star).OnlyEnforceIf(match_dx.Not());
                model.AddEquality(diff_y, dy_star).OnlyEnforceIf(match_dy);
                model.AddNotEqual(diff_y, dy_star).OnlyEnforceIf(match_dy.Not());

                model.AddBoolOr({match_dx.Not(), match_dy.Not(),
                                 rotB[u][ru].Not(), rotB[v][rv].Not()});
            }
        }

        // Pairwise cuts for streams sharing a conflicted cell
        for (const auto& [cx, cy] : final_conflicted_cells)
        {
            std::vector<int> s_indices;
            for (const auto& o : cell_occ[cx][cy][0])
                s_indices.push_back(o.stream_idx);
            for (const auto& o : cell_occ[cx][cy][1])
                s_indices.push_back(o.stream_idx);

            if (s_indices.size() >= 2)
            {
                int u1 = streams[s_indices[0]].src_entity;
                int v1 = streams[s_indices[0]].dst_entity;
                int u2 = streams[s_indices[1]].src_entity;
                int v2 = streams[s_indices[1]].dst_entity;
                std::vector<BoolVar> cut_terms;
                if (u1 >= 0 && u1 < N)
                    cut_terms.push_back(entity_matches[u1].Not());
                if (v1 >= 0 && v1 < N)
                    cut_terms.push_back(entity_matches[v1].Not());
                if (u2 >= 0 && u2 < N)
                    cut_terms.push_back(entity_matches[u2].Not());
                if (v2 >= 0 && v2 < N)
                    cut_terms.push_back(entity_matches[v2].Not());
                if (!cut_terms.empty())
                {
                    model.AddBoolOr(cut_terms);
                }
            }
        }
    }

    return false;
}
