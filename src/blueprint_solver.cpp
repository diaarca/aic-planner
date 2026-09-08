#include "blueprint_solver.hpp"
#include "ortools/sat/cp_model.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <queue>

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
        return "SP";
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
            ci.fg = 231; // Bright white for 4-way crossings
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
    std::cout << "  Turns: ╔═ ╗ ╚═ ╝ | Crossing: \033[38;5;231m╬═\033[0m\n";
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

    // 1. Unloaders
    int total_unloaders = 0;
    std::map<std::string, std::vector<int>> unloader_ids_by_mineral;
    for (const auto& [m, rate] : mineral_flows)
    {
        int num_u = static_cast<int>(std::ceil(rate / 2.0));
        total_unloaders += num_u;
        for (int u = 0; u < num_u; ++u)
        {
            BlueprintEntityToPlace ent;
            ent.id = next_id++;
            ent.type = "unloader";
            ent.name = "unloader_" + m + "_" + std::to_string(u + 1);
            ent.base_w = 3;
            ent.base_h = 1;
            ent.is_unloader = true;
            ent.item_produced = m;
            ent.outputs.push_back(
                {1, 0, "product", 0, -1}); // middle output facing South
            unloader_ids_by_mineral[m].push_back(ent.id);
            entities.push_back(ent);
        }
    }

    // 2. Intermediate facilities
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

    // 3. Root facility
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

    // 4. Protocol Stash
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
        int in_p = used_in_port[dst]++;
        streams.push_back({src, out_p, dst, in_p, item, rate});
    };

    // Connect unloaders to intermediate facilities consuming mineral
    for (const auto& [m, u_ids] : unloader_ids_by_mineral)
    {
        for (auto& ent : entities)
        {
            if (ent.item_consumed == m && !ent.is_unloader)
            {
                int u_id = u_ids[0];
                add_stream(u_id, ent.id, m, 0.5);
            }
        }
        // Also check root facility
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == m)
            {
                add_stream(u_ids[0], root_entity_id, m, 0.5);
            }
        }
    }

    // Connect intermediate facilities to root facility or higher intermediate
    for (const auto& [item, f_ids] : fac_ids_by_produced_item)
    {
        // Check if root facility consumes it
        bool consumed_by_root = false;
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == item)
            {
                consumed_by_root = true;
                break;
            }
        }
        if (consumed_by_root)
        {
            for (int fid : f_ids)
            {
                add_stream(fid, root_entity_id, item, 0.5);
            }
        }
        else
        {
            // Consumed by another intermediate
            for (auto& ent : entities)
            {
                if (ent.item_consumed == item && ent.id != root_entity_id &&
                    !ent.is_unloader)
                {
                    add_stream(f_ids[0], ent.id, item, 0.5);
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

    // Non-overlapping
    NoOverlap2DConstraint no_overlap = model.AddNoOverlap2D();
    for (size_t i = 0; i < intX.size(); ++i)
    {
        no_overlap.AddRectangle(intX[i], intY[i]);
    }

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

    IntVar area = model.NewIntVar(Domain(1, grid_w * grid_h)).WithName("area");
    model.AddMultiplicationEquality(area, bbox_w, bbox_h);

    // Minimize Manhattan conveyor distances as regularization
    LinearExpr stream_dist;
    for (const auto& s : streams)
    {
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

        const auto* src_pa = find_access(s.src_entity, false, s.src_port_idx);
        const auto* dst_pa = find_access(s.dst_entity, true, s.dst_port_idx);
        if (src_pa && dst_pa)
        {
            IntVar dx = model.NewIntVar(Domain(0, grid_w));
            IntVar dy = model.NewIntVar(Domain(0, grid_h));
            model.AddAbsEquality(dx, src_pa->ax - dst_pa->ax);
            model.AddAbsEquality(dy, src_pa->ay - dst_pa->ay);
            stream_dist += dx + dy;
        }
    }

    // Objective: prioritize area, then compact depot-facing width, then height & wirelength
    model.Minimize(area * 100 + bbox_w * 20 + bbox_h * 10 + stream_dist);

    SatParameters params;
    params.set_max_time_in_seconds(10.0);
    params.set_num_search_workers(8);
    params.set_random_seed(42);

    const int max_conveyor_attempts = 50;

    for (int attempt = 1; attempt <= max_conveyor_attempts; ++attempt)
    {
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
                    {pe.x + px, pe.y + py, is_in, fx, fy, p.type});
            };

            for (const auto& in_p : entities[i].inputs)
                add_placed_port(in_p, true);
            for (const auto& out_p : entities[i].outputs)
                add_placed_port(out_p, false);

            layout.entities.push_back(pe);
        }

        // -------------------------------------------------------------
        // Conveyor Routing & Feasibility Check
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

        std::vector<int> failed_stream_indices;

        for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
        {
            const auto& s = streams[s_idx];
            if (s.src_entity >= static_cast<int>(layout.entities.size()) ||
                s.dst_entity >= static_cast<int>(layout.entities.size()))
            {
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
                continue;
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
                continue;
            const auto& dst_port =
                dst_inputs[s.dst_port_idx % dst_inputs.size()];

            int src_ax = src_port.x + src_port.fx;
            int src_ay = src_port.y + src_port.fy;
            int dst_ax = dst_port.x + dst_port.fx;
            int dst_ay = dst_port.y + dst_port.fy;

            std::queue<std::pair<int, int>> q;
            std::map<std::pair<int, int>, std::pair<int, int>> came_from;
            std::set<std::pair<int, int>> visited;

            std::pair<int, int> start_pt = {src_ax, src_ay};
            std::pair<int, int> target_pt = {dst_ax, dst_ay};

            q.push(start_pt);
            visited.insert(start_pt);

            bool found = (start_pt == target_pt);
            while (!q.empty() && !found)
            {
                auto curr = q.front();
                q.pop();

                if (curr == target_pt)
                {
                    found = true;
                    break;
                }

                const int dxy[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
                for (int d = 0; d < 4; ++d)
                {
                    int nx = curr.first + dxy[d][0];
                    int ny = curr.second + dxy[d][1];
                    std::pair<int, int> next_pt = {nx, ny};

                    if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h)
                        continue;
                    if (facility_cells.count(next_pt))
                        continue; // No overlap with facilities
                    if (visited.count(next_pt))
                        continue;

                    visited.insert(next_pt);
                    came_from[next_pt] = curr;
                    if (next_pt == target_pt)
                    {
                        found = true;
                        break;
                    }
                    q.push(next_pt);
                }
            }

            if (!found)
            {
                layout.is_conveyor_feasible = false;
                failed_stream_indices.push_back(static_cast<int>(s_idx));
                std::cerr << "[WARNING] Conveyor path infeasible for stream: "
                          << s.item << " from " << pe_src.name << " to "
                          << pe_dst.name << std::endl;
                continue;
            }

            // Reconstruct path
            std::vector<std::pair<int, int>> path;
            auto curr = target_pt;
            while (curr != start_pt)
            {
                path.push_back(curr);
                curr = came_from[curr];
            }
            path.push_back(start_pt);
            std::reverse(path.begin(), path.end());

            // Connect path segments into layout.conveyor_cells
            for (size_t k = 0; k < path.size(); ++k)
            {
                auto pt = path[k];
                if (k == 0)
                {
                    int to_src_dx = -src_port.fx;
                    int to_src_dy = -src_port.fy;
                    layout.conveyor_cells[pt] |=
                        get_dir_mask(to_src_dx, to_src_dy);
                }
                else
                {
                    int prev_dx = path[k - 1].first - pt.first;
                    int prev_dy = path[k - 1].second - pt.second;
                    layout.conveyor_cells[pt] |= get_dir_mask(prev_dx, prev_dy);
                }

                if (k == path.size() - 1)
                {
                    int to_dst_dx = -dst_port.fx;
                    int to_dst_dy = -dst_port.fy;
                    layout.conveyor_cells[pt] |=
                        get_dir_mask(to_dst_dx, to_dst_dy);
                }
                else
                {
                    int next_dx = path[k + 1].first - pt.first;
                    int next_dy = path[k + 1].second - pt.second;
                    layout.conveyor_cells[pt] |= get_dir_mask(next_dx, next_dy);
                }

                layout.conveyor_items[pt] = s.item;
            }

            // Assign item to src and dst ports for color visualization
            for (auto& p : layout.entities[s.src_entity].ports)
            {
                if (p.x == src_port.x && p.y == src_port.y)
                {
                    p.item = s.item;
                    break;
                }
            }
            for (auto& p : layout.entities[s.dst_entity].ports)
            {
                if (p.x == dst_port.x && p.y == dst_port.y)
                {
                    p.item = s.item;
                    break;
                }
            }
        }

        // Check if all streams are feasible
        if (layout.is_conveyor_feasible)
        {
            if (attempt > 1)
            {
                std::cout << "[OK] Conveyor routing feasible for "
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

        // Infeasible: Add no-good cuts to CP-SAT model and retry!
        std::cout << "[INFO] Subproblem conveyor check failed for "
                  << product.name << " on attempt " << attempt << " ("
                  << failed_stream_indices.size()
                  << " failed streams). Repeating subproblem with no-good cuts...\n";

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

        // Targeted cuts on failed streams: forbid src and dst both staying in current position/rotation
        for (int s_idx : failed_stream_indices)
        {
            int u = streams[s_idx].src_entity;
            int v = streams[s_idx].dst_entity;
            if (u >= 0 && u < N && v >= 0 && v < N)
            {
                model.AddBoolOr(
                    {entity_matches[u].Not(), entity_matches[v].Not()});
            }
        }
    }

    return false;
}
