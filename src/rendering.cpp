#include "blueprint_layout.hpp"
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>

namespace aic
{
using namespace operations_research::sat;
int get_facility_color(const std::string &type)
{
    if (type == "power_diffuser_1")
        return 226;
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

int get_item_color(const std::string &item)
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

std::string get_facility_code(const std::string &type, int instance_idx = 0)
{
    if (type == "power_diffuser_1")
        return "EP";
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
    for (const auto &e : entities)
    {
        min_x = std::min(min_x, e.x);
        max_x = std::max(max_x, e.x + e.width - 1);
        min_y = std::min(min_y, e.y);
        max_y = std::max(max_y, e.y + e.height - 1);
    }
    for (const auto &[cell, mask] : conveyor_cells)
    {
        min_x = std::min(min_x, cell.first);
        max_x = std::max(max_x, cell.first);
        min_y = std::min(min_y, cell.second);
        max_y = std::max(max_y, cell.second);
    }

    std::cout << "\n\033[1;36m================================================="
                 "===============================\033[0m\n";
    std::cout << "\033[1;37m  BLUEPRINT 2D TERMINAL MAP: \033[1;33m" << product_name << "\033[0m\n";
    std::cout << "  Bounding Box: \033[1m" << bounding_width << " x " << bounding_height
              << "\033[0m (Area: \033[1;32m" << (bounding_width * bounding_height) << "\033[0m) | "
              << "Depot Length: \033[1;35m" << depot_length << "\033[0m\n";
    std::cout << "\033[1;36m==================================================="
                 "=============================\033[0m\n\n";

    std::cout << "  Net output: " << output_per_minute << " items/min\n";

    struct CellInfo
    {
        int bg = -1;
        int fg = 16;
        std::string text = " ·";
    };
    std::map<std::pair<int, int>, CellInfo> grid;

    for (const auto &e : entities)
    {
        int cx = e.x + e.width / 2;
        int cy = e.y + e.height / 2;
        int fg = (e.color_code == 33 || e.color_code == 160 || e.color_code == 34) ? 15 : 16;

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

                for (const auto &p : e.ports)
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
                            ci.fg = get_item_color(p.item);
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
    for (const auto &[cell, mask] : conveyor_cells)
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

        if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST) && (mask & CONV_WEST))
        {
            ci.fg = 201; // Bright Magenta / Hot Pink for 4-way bridge crossings
        }
        else if (!item.empty())
        {
            ci.fg = get_item_color(item);
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
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST) &&
                 (mask & CONV_WEST))
            sym = "╬═";
        else if ((mask & CONV_EAST) && (mask & CONV_WEST) && (mask & CONV_NORTH))
            sym = "╩═";
        else if ((mask & CONV_EAST) && (mask & CONV_WEST) && (mask & CONV_SOUTH))
            sym = "╦═";
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST))
            sym = "╠═";
        else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_WEST))
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
    for (const auto &e : entities)
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
                    std::cout << "\033[48;5;" << it->second.bg << "m\033[38;5;" << it->second.fg
                              << "m" << it->second.text << "\033[0m";
                }
                else
                {
                    // Conveyor tile with item color
                    std::cout << "\033[38;5;" << it->second.fg << "m" << it->second.text
                              << "\033[0m";
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
    for (const auto &e : entities)
    {
        std::cout << "  \033[48;5;" << e.color_code << "m  \033[0m "
                  << "\033[1m[" << std::left << std::setw(2) << e.code << "]\033[0m " << std::left
                  << std::setw(26) << e.name << " | Size: " << std::setw(4)
                  << (std::to_string(e.width) + "x" + std::to_string(e.height))
                  << " | Rot: " << std::right << std::setw(3) << e.rotation << "°"
                  << " | Pos: (" << std::setw(2) << e.x << "," << std::setw(2) << e.y << ")";
        if (e.is_unloader)
        {
            std::cout << " [Mineral: " << e.mineral << "]";
        }
        if (!e.seed_loop.empty())
            std::cout << " | Seed loop: " << e.seed_loop;
        if (!e.recipe_name.empty())
            std::cout << " | Recipe: " << e.recipe_name
                      << " | Utilization: " << std::round(e.utilization * 100) << '%';
        std::cout << "\n";
    }
    if (!startup_items.empty())
    {
        std::cout << "  Initial stock required to start recycling loops:";
        for (const auto &item : startup_items)
            std::cout << ' ' << startup_stock.at(item) << 'x' << item;
        std::cout << '\n';
    }

    std::set<std::string> unique_items;
    for (const auto &[cell, item] : conveyor_items)
    {
        if (!item.empty())
            unique_items.insert(item);
    }
    if (!unique_items.empty())
    {
        std::cout << "  Conveyors: ";
        for (const auto &item : unique_items)
        {
            int color = get_item_color(item);
            std::cout << "\033[38;5;" << color << "m══ " << item << "\033[0m   ";
        }
        std::cout << "\n";
    }
    std::cout << "  Turns: ╔═ ╗ ╚═ ╝ | Crossing (Bridge): \033[38;5;201m╬═\033[0m\n";
    std::cout << "  Port Arrows: ▲ ▼ ► ◄ (Output = exit facility, Input = enter facility, colored "
                 "by item)\n";
    std::cout << "\033[1;36m==================================================="
                 "=============================\033[0m\n";
}

void Solver::display_solution()
{
    std::cout << "Solution Status: " << status() << std::endl;
    std::cout << "Objective Value (Net Value per Minute): "
              << (response_.objective_value() / static_cast<double>(obj_scale_factor_))
              << std::endl;

    std::cout << "\n--- Production Plan (units per minute) ---" << std::endl;
    for (size_t p = 0; p < products_.size(); ++p)
    {
        int64_t produced = SolutionIntegerValue(response_, qty_produced_[p]);
        if (produced > 0)
        {
            int64_t total_factories = 0;
            for (size_t a = 0; a < areas_.size(); ++a)
                total_factories += SolutionIntegerValue(response_, factories_in_area_[p][a]);
            std::cout << products_[p].name << ": " << produced << " units [" << total_factories
                      << " factories]" << std::endl;
        }
    }

    std::cout << "\n--- Factory Placement ---" << std::endl;
    for (size_t a = 0; a < areas_.size(); ++a)
    {
        bool area_used = false;
        for (size_t p = 0; p < products_.size(); ++p)
        {
            if (SolutionIntegerValue(response_, factories_in_area_[p][a]) > 0)
            {
                area_used = true;
                break;
            }
        }
        if (area_used)
        {
            std::cout << "Area: " << areas_[a].name << std::endl;
            double used_space = 0;
            double used_depot = 0;
            for (size_t p = 0; p < products_.size(); ++p)
            {
                int64_t num_f = SolutionIntegerValue(response_, factories_in_area_[p][a]);
                if (num_f > 0)
                {
                    std::cout << "  - " << products_[p].name << ": " << num_f << " factories"
                              << std::endl;
                    used_space +=
                        num_f * (products_[p].factory_width * products_[p].factory_height);
                    used_depot += num_f * products_[p].factory_depot;
                }
            }
            std::cout << "  Space used: " << used_space << " / "
                      << (areas_[a].pac_width * areas_[a].pac_height) << std::endl;

            std::cout << "  Depot length used: " << used_depot << " / "
                      << areas_[a].pac_depot_width + areas_[a].pac_depot_height << std::endl;
        }
    }

    std::cout << "\n--- Mineral Consumption (usage / limit) ---" << std::endl;
    for (const auto &mineral : mineral_limits_)
    {
        double total_consumed = 0.0;
        for (size_t p = 0; p < products_.size(); ++p)
        {
            if (products_[p].mineral_consumption.count(mineral.name))
                total_consumed += products_[p].mineral_consumption.at(mineral.name) *
                                  SolutionIntegerValue(response_, qty_produced_[p]);
        }
        for (size_t fu = 0; fu < fuels_.size(); ++fu)
        {
            if (fuels_[fu].name == mineral.name)
                total_consumed += SolutionIntegerValue(response_, num_batteries_active_[fu]) *
                                  (60.0 / fuels_[fu].duration);
        }
        std::cout << mineral.name << ": " << total_consumed << " / " << mineral.limit << std::endl;
    }

    std::cout << "\n--- Power Consumption ---" << std::endl;
    double infrastructure_power = 0, factory_power = 0;
    for (const auto &area : areas_)
        infrastructure_power += facility_power_consumption(area.area_facilities, facility_power_);
    for (size_t p = 0; p < products_.size(); ++p)
    {
        const double f_pow =
            facility_power_consumption(products_[p].factory_facilities, facility_power_);
        for (size_t a = 0; a < areas_.size(); ++a)
            factory_power += SolutionIntegerValue(response_, factories_in_area_[p][a]) * f_pow;
    }
    std::cout << "Power for Infrastructure: " << infrastructure_power
              << "\nPower for Factories: " << factory_power << '\n';
    std::cout << "Total Power Needed: " << (infrastructure_power + factory_power) << '\n';

    std::cout << "\n--- Power Production (Battery Mix) ---\n";
    double t_supp = 0;
    for (size_t fu = 0; fu < fuels_.size(); ++fu)
    {
        int64_t num = SolutionIntegerValue(response_, num_batteries_active_[fu]);
        if (num > 0)
        {
            double s = num * fuels_[fu].power;
            std::cout << fuels_[fu].name << ": " << num << " active batteries (" << s << " power, "
                      << num * (60.0 / fuels_[fu].duration) << " units/min consumption)"
                      << std::endl;
            t_supp += s;
        }
    }
    std::cout << "Total Power Provided: " << t_supp << std::endl;

    if (!product_layouts_.empty())
    {
        if (render_layouts_)
            display_area_layouts();
    }
}

void Solver::display_area_layouts()
{
    for (size_t a = 0; a < areas_.size(); ++a)
    {
        int W_a = areas_[a].pac_width;
        int H_a = areas_[a].pac_height;
        if (W_a <= 0 || H_a <= 0)
            continue;

        struct CellInfo
        {
            int bg = -1;
            int fg = 16;
            std::string text = " ·";
        };
        std::map<std::pair<int, int>, CellInfo> grid;
        std::map<std::pair<int, int>, int> conveyor_cells;
        std::map<std::pair<int, int>, std::string> conveyor_items;

        int active_count = 0;
        std::vector<std::string> placed_summary;

        if (a < area_slots_.size())
        {
            for (size_t k = 0; k < area_slots_[a].size(); ++k)
            {
                const auto &slot = area_slots_[a][k];
                if (!SolutionBooleanValue(response_, slot.active))
                    continue;

                active_count++;
                int p_idx = -1;
                for (size_t p = 0; p < products_.size(); ++p)
                {
                    if (SolutionBooleanValue(response_, slot.is_prod[p]))
                    {
                        p_idx = p;
                        break;
                    }
                }
                if (p_idx < 0)
                    continue;

                int rot = 0;
                for (int r : {0, 90, 180, 270})
                {
                    if (SolutionBooleanValue(response_, slot.is_rot.at(r)))
                    {
                        rot = r;
                        break;
                    }
                }

                int bx = SolutionIntegerValue(response_, slot.bx);
                int by = SolutionIntegerValue(response_, slot.by);
                int bw = SolutionIntegerValue(response_, slot.bw);
                int bh = SolutionIntegerValue(response_, slot.bh);

                const auto &layout = product_layouts_.at(products_[p_idx].name);
                int W_0 = layout.bounding_width;
                int H_0 = layout.bounding_height;

                placed_summary.push_back(products_[p_idx].name + " [" + std::to_string(bw) + "x" +
                                         std::to_string(bh) + " @ (" + std::to_string(bx) + "," +
                                         std::to_string(by) + ") rot:" + std::to_string(rot) +
                                         "°]");

                // Place entities
                for (const auto &entity : layout.entities)
                {
                    const auto placed = rotate_entity(entity, W_0, H_0, rot);
                    const int gx = bx + placed.x, gy = by + placed.y;
                    const int ew = placed.width, eh = placed.height;
                    int cx = gx + ew / 2;
                    int cy = gy + eh / 2;
                    int fg = (entity.color_code == 33 || entity.color_code == 160 ||
                              entity.color_code == 34)
                                 ? 15
                                 : 16;

                    for (int dy = 0; dy < eh; ++dy)
                    {
                        for (int dx = 0; dx < ew; ++dx)
                        {
                            int cur_x = gx + dx;
                            int cur_y = gy + dy;
                            CellInfo ci;
                            ci.bg = entity.color_code;
                            ci.fg = fg;
                            ci.text = "  ";
                            if (cur_x == cx && cur_y == cy)
                            {
                                ci.text = entity.code;
                                if (ci.text.size() == 1)
                                    ci.text += " ";
                            }
                            grid[{cur_x, cur_y}] = ci;
                        }
                    }

                    // Place ports
                    for (const auto &p : entity.ports)
                    {
                        const auto placed_port = rotate_port(p, W_0, H_0, rot);
                        const int rfx = placed_port.fx, rfy = placed_port.fy;
                        const int port_gx = bx + placed_port.x, port_gy = by + placed_port.y;
                        CellInfo &pci = grid[{port_gx, port_gy}];
                        if (!p.item.empty())
                        {
                            pci.fg = get_item_color(p.item);
                        }
                        if (p.is_input)
                        {
                            if (rfy == 1)
                                pci.text = "▼ ";
                            else if (rfy == -1)
                                pci.text = "▲ ";
                            else if (rfx == 1)
                                pci.text = "◄ ";
                            else if (rfx == -1)
                                pci.text = "► ";
                        }
                        else
                        {
                            if (rfy == 1)
                                pci.text = "▲ ";
                            else if (rfy == -1)
                                pci.text = "▼ ";
                            else if (rfx == 1)
                                pci.text = "► ";
                            else if (rfx == -1)
                                pci.text = "◄ ";
                        }
                    }
                }

                // Place conveyors
                for (const auto &[cell, mask] : layout.conveyor_cells)
                {
                    const auto [rcx, rcy] = rotate_cell(cell.first, cell.second, W_0, H_0, rot);
                    const int rmask = rotate_conveyor_mask(mask, rot);

                    conveyor_cells[{bx + rcx, by + rcy}] |= rmask;
                    auto it_item = layout.conveyor_items.find(cell);
                    if (it_item != layout.conveyor_items.end())
                    {
                        conveyor_items[{bx + rcx, by + rcy}] = it_item->second;
                    }
                }
            }
        }

        if (active_count == 0)
        {
            std::cout << "\n\033[1;36m========================================="
                         "========"
                         "===============================\033[0m\n";
            std::cout << "\033[1;37m  GLOBAL 2D INDUSTRIAL COMPLEX: AREA \033[1;33m"
                      << areas_[a].name << " (" << W_a << "x" << H_a
                      << ")\033[0m - \033[1;30mNO FACTORIES ALLOCATED\033[0m\n";
            std::cout << "\033[1;36m==========================================="
                         "========"
                         "=============================\033[0m\n";
            continue;
        }

        // Stamp conveyors on grid
        for (const auto &[cell, mask] : conveyor_cells)
        {
            if (grid.count(cell))
                continue;

            CellInfo ci;
            ci.bg = -1;

            std::string item = "";
            auto it_item = conveyor_items.find(cell);
            if (it_item != conveyor_items.end())
            {
                item = it_item->second;
            }

            if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST) &&
                (mask & CONV_WEST))
            {
                ci.fg = 201; // Bright Magenta / Hot Pink for 4-way bridge crossings
            }
            else if (!item.empty())
            {
                ci.fg = get_item_color(item);
            }
            else
            {
                ci.fg = 220;
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
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST) &&
                     (mask & CONV_WEST))
                sym = "╬═";
            else if ((mask & CONV_EAST) && (mask & CONV_WEST) && (mask & CONV_NORTH))
                sym = "╩═";
            else if ((mask & CONV_EAST) && (mask & CONV_WEST) && (mask & CONV_SOUTH))
                sym = "╦═";
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_EAST))
                sym = "╠═";
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) && (mask & CONV_WEST))
                sym = "╣ ";
            else if (mask & (CONV_NORTH | CONV_SOUTH))
                sym = "║ ";
            else if (mask & (CONV_EAST | CONV_WEST))
                sym = "══";

            ci.text = sym;
            grid[cell] = ci;
        }

        // Print Global Area Map
        std::cout << "\n\033[1;36m================================================="
                     "===============================\033[0m\n";
        std::cout << "\033[1;37m  GLOBAL 2D INDUSTRIAL COMPLEX: AREA \033[1;33m" << areas_[a].name
                  << " (" << W_a << " x " << H_a << ")\033[0m\n";
        std::cout << "  Factories Placed: \033[1;32m" << active_count << "\033[0m\n";
        for (const auto &s : placed_summary)
        {
            std::cout << "   • " << s << "\n";
        }
        std::cout << "\033[1;36m==================================================="
                     "=============================\033[0m\n\n";

        // Top depot line banner
        bool has_top_depot = false;
        bool has_right_depot = false;
        for (const auto &d : areas_[a].static_depots)
        {
            if (d.y1 == H_a && d.y2 == H_a)
                has_top_depot = true;
            if (d.x1 == W_a && d.x2 == W_a)
                has_right_depot = true;
        }

        if (has_top_depot)
        {
            std::cout << "        \033[1;33m";
            for (int x = 0; x < W_a; ++x)
                std::cout << "══";
            std::cout << " [TOP STATIC DEPOT LINE]\033[0m\n";
        }

        // X-axis tens
        if (W_a >= 10)
        {
            std::cout << "        ";
            for (int x = 0; x < W_a; ++x)
            {
                int t = x / 10;
                if (t > 0)
                    std::cout << " " << t;
                else
                    std::cout << "  ";
            }
            std::cout << "\n";
        }
        // X-axis ones
        std::cout << "        ";
        for (int x = 0; x < W_a; ++x)
        {
            std::cout << " " << (x % 10);
        }
        std::cout << "\n";

        // Top border
        std::cout << "      ┌";
        for (int x = 0; x < W_a; ++x)
            std::cout << "──";
        std::cout << "┐\n";

        // Rows
        for (int y = H_a - 1; y >= 0; --y)
        {
            std::cout << "  " << std::setw(2) << y << "  │";
            for (int x = 0; x < W_a; ++x)
            {
                auto it = grid.find({x, y});
                if (it != grid.end())
                {
                    if (it->second.bg >= 0)
                    {
                        std::cout << "\033[48;5;" << it->second.bg << "m\033[38;5;" << it->second.fg
                                  << "m" << it->second.text << "\033[0m";
                    }
                    else
                    {
                        std::cout << "\033[38;5;" << it->second.fg << "m" << it->second.text
                                  << "\033[0m";
                    }
                }
                else
                {
                    std::cout << "\033[38;5;239m ·\033[0m";
                }
            }
            std::cout << "│  " << std::setw(2) << y;
            if (has_right_depot)
            {
                std::cout << " \033[1;33m║ [DEPOT]\033[0m";
            }
            std::cout << "\n";
        }

        // Bottom border
        std::cout << "      └";
        for (int x = 0; x < W_a; ++x)
            std::cout << "──";
        std::cout << "┘\n";

        // Bottom X-axis
        std::cout << "        ";
        for (int x = 0; x < W_a; ++x)
        {
            std::cout << " " << (x % 10);
        }
        std::cout << "\n\n";

        // Print Facility Legend for Area
        std::cout << "\033[1;36m---------------------------------------------------"
                     "-----------------------------\033[0m\n";
        std::cout << "\033[1;37m  AREA FACILITY LEGEND (Unified Colors)\033[0m\n";
        std::cout << "\033[1;36m---------------------------------------------------"
                     "-----------------------------\033[0m\n";
        std::set<std::string> facility_types;
        for (const auto &[name, layout] : product_layouts_)
            for (const auto &entity : layout.entities)
                facility_types.insert(entity.type);
        for (const auto &type : facility_types)
            std::cout << "  \033[48;5;" << get_facility_color(type) << "m  \033[0m ["
                      << get_facility_code(type, 0) << "] " << type << '\n';

        std::set<std::string> unique_area_items;
        for (const auto &[cell, item] : conveyor_items)
        {
            if (!item.empty())
                unique_area_items.insert(item);
        }
        if (!unique_area_items.empty())
        {
            std::cout << "  Conveyors: ";
            for (const auto &item : unique_area_items)
            {
                int color = get_item_color(item);
                std::cout << "\033[38;5;" << color << "m══ " << item << "\033[0m   ";
            }
            std::cout << "\n";
        }
        std::cout << "  Turns: ╔═ ╗ ╚═ ╝ | Crossing (Bridge): "
                     "\033[38;5;201m╬═\033[0m\n";
        std::cout << "  Port Arrows: ▲ ▼ ► ◄ (Output = exit facility, Input = "
                     "enter facility, colored by item)\n";
        std::cout << "\033[1;36m==================================================="
                     "=============================\033[0m\n\n";
    }
}

} // namespace aic
