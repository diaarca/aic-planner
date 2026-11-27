#include "solver.hpp"
#include "logging.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace aic
{

using namespace operations_research;
using namespace operations_research::sat;

Solver::Solver(const std::vector<Product> &products, const std::vector<Mineral> &mineral_limits,
               const std::vector<Area> &areas, const std::vector<Fuel> &fuels,
               const std::map<std::string, double> &facility_power, const Region &region)
    : products_(products), mineral_limits_(mineral_limits), areas_(areas), fuels_(fuels),
      facility_power_(facility_power), region_(region), obj_scale_factor_(60)
{
}

void Solver::set_blueprint_layouts(const std::map<std::string, BlueprintLayout> &layouts)
{
    product_layouts_ = layouts;
}

bool Solver::solve()
{
    if (product_layouts_.size() != products_.size())
        throw std::invalid_argument("Every product requires a routed blueprint");
    // Compute scaling factor for fuel duration to keep all coefficients integer
    int64_t scale = 1;
    for (const auto &fuel : fuels_)
    {
        if (fuel.duration > 0)
        {
            const int64_t duration = static_cast<int64_t>(fuel.duration);
            const int64_t factor = duration / std::gcd(scale, duration);
            if (factor > 1000000 / scale)
                throw std::invalid_argument("Fuel duration scaling exceeds supported precision");
            scale *= factor;
        }
    }
    obj_scale_factor_ = scale * 1000;

    cp_model_ = CpModelBuilder{};
    instantiate_variables();
    declare_constraints();

    if (solve_model())
    {
        display_solution();
        return true;
    }
    else
    {
        log(LogLevel::Error, "Regional solver status: ", status(), "; ", response_.solution_info());
        return false;
    }
}

void Solver::instantiate_variables()
{
    // Gross fuel production may exceed the inventory bound because some is burned.
    // Bound factories by physical capacity, then derive production/burn domains.
    const int64_t max_inventory_rate = std::ceil(region_.storage / 60 / inventory_horizon_hours);
    std::vector<int64_t> production_bounds;
    std::vector<std::vector<int64_t>> factory_bounds(products_.size());
    for (size_t p = 0; p < products_.size(); ++p)
    {
        const auto &layout = product_layouts_.at(products_[p].layout_key());
        if (layout.bounding_width <= 0 || layout.bounding_height <= 0 || layout.depot_length < 0)
            throw std::invalid_argument("Invalid blueprint bounds for " + products_[p].name);
        int64_t total_factories = 0;
        for (const auto &area : areas_)
        {
            const int64_t by_area = static_cast<int64_t>(area.pac_width * area.pac_height) /
                                    (layout.bounding_width * layout.bounding_height);
            int64_t capacity = by_area;
            if (!products_[p].candidate_area.empty() && products_[p].candidate_area != area.name)
                capacity = 0;
            else if (!layout.fixed_area.empty())
                capacity = layout.fixed_area == area.name ? std::min<int64_t>(capacity, 1) : 0;
            else if (layout.depot_length > 0)
                capacity = std::min(
                    capacity, static_cast<int64_t>(area.pac_depot_width + area.pac_depot_height) /
                                  layout.depot_length);
            factory_bounds[p].push_back(capacity);
            total_factories += capacity;
        }
        int64_t capacity_rate =
            static_cast<int64_t>(std::ceil(total_factories * 60.0 / products_[p].production_time));
        const bool fuel =
            !products_[p].rebuilding &&
            std::any_of(fuels_.begin(), fuels_.end(),
                        [&](const Fuel &value) { return value.name == products_[p].name; });
        production_bounds.push_back(fuel || products_[p].rebuilding
                                        ? capacity_rate
                                        : std::min(capacity_rate, max_inventory_rate));
    }
    qty_produced_.clear();
    factories_in_area_.clear();
    factories_in_area_.resize(products_.size());
    for (size_t p = 0; p < products_.size(); ++p)
    {
        qty_produced_.push_back(cp_model_.NewIntVar(Domain(0, production_bounds[p]))
                                    .WithName(products_[p].name + "_prod"));
        for (size_t a = 0; a < areas_.size(); ++a)
            factories_in_area_[p].push_back(
                cp_model_.NewIntVar(Domain(0, factory_bounds[p][a]))
                    .WithName(products_[p].name + "_" + areas_[a].name));
    }
    num_batteries_active_.clear();
    for (const auto &fuel : fuels_)
    {
        int64_t bound = 0;
        for (size_t p = 0; p < products_.size(); ++p)
            if (!products_[p].rebuilding && products_[p].name == fuel.name)
                bound +=
                    static_cast<int64_t>(std::ceil(production_bounds[p] * fuel.duration / 60.0));
        num_batteries_active_.push_back(
            cp_model_.NewIntVar(Domain(0, bound)).WithName(fuel.name + "_active"));
    }

    instantiate_area_slots();
}

void Solver::instantiate_area_slots()
{
    area_slots_.clear();
    area_slots_.resize(areas_.size());
    if (!product_layouts_.empty())
    {
        // Dynamic upper bound on slots based on the smallest candidate
        // blueprint
        int min_blueprint_area = std::numeric_limits<int>::max();
        int min_blueprint_depot = std::numeric_limits<int>::max();
        int min_blueprint_min_dim = std::numeric_limits<int>::max();

        for (const auto &[name, layout] : product_layouts_)
        {
            int area = layout.bounding_width * layout.bounding_height;
            if (area > 0 && area < min_blueprint_area)
            {
                min_blueprint_area = area;
            }
            if (layout.depot_length == 0 || !layout.fixed_area.empty())
                min_blueprint_depot = 0;
            else if (layout.depot_length < min_blueprint_depot)
            {
                min_blueprint_depot = layout.depot_length;
            }
            int min_d = std::min(layout.bounding_width, layout.bounding_height);
            if (min_d > 0 && min_d < min_blueprint_min_dim)
            {
                min_blueprint_min_dim = min_d;
            }
        }
        if (min_blueprint_area == std::numeric_limits<int>::max() || min_blueprint_area <= 0)
        {
            min_blueprint_area = 1;
        }

        for (size_t a = 0; a < areas_.size(); ++a)
        {
            int W_a = areas_[a].pac_width;
            int H_a = areas_[a].pac_height;
            if (W_a <= 0 || H_a <= 0)
                continue;

            // Compute dynamic slot capacity K_a based on area footprint and
            // depot length
            int max_by_area = (W_a * H_a) / min_blueprint_area;
            int total_depot =
                static_cast<int>(areas_[a].pac_depot_width + areas_[a].pac_depot_height);

            int K = max_by_area;
            int anchored = 0;
            bool allows_movable = false;
            for (const auto &product : products_)
            {
                if (!product.candidate_area.empty() && product.candidate_area != areas_[a].name)
                    continue;
                const auto &layout = product_layouts_.at(product.layout_key());
                if (layout.fixed_area.empty())
                    allows_movable = true;
                else if (layout.fixed_area == areas_[a].name)
                    ++anchored;
            }
            if (!allows_movable)
                K = anchored;
            if (min_blueprint_depot > 0 && min_blueprint_depot != std::numeric_limits<int>::max())
            {
                if (total_depot > 0)
                {
                    int max_by_depot = total_depot / min_blueprint_depot;
                    K = std::min(K, max_by_depot);
                }
                else
                {
                    // If factories require depots but area has 0 depot length,
                    // 0 can fit
                    K = 0;
                }
            }
            if (min_blueprint_min_dim != std::numeric_limits<int>::max() &&
                std::max(W_a, H_a) < min_blueprint_min_dim)
            {
                K = 0;
            }

            for (int k = 0; k < K; ++k)
            {
                AreaSlotVar slot;
                std::string prefix = "a" + std::to_string(a) + "s_" + std::to_string(k);
                slot.active = cp_model_.NewBoolVar().WithName(prefix + "act_");
                for (size_t p = 0; p < products_.size(); ++p)
                {
                    slot.is_prod.push_back(
                        cp_model_.NewBoolVar().WithName(prefix + "p_" + std::to_string(p)));
                }
                slot.is_rot[0] = cp_model_.NewBoolVar().WithName(prefix + "r0_");
                slot.is_rot[90] = cp_model_.NewBoolVar().WithName(prefix + "r90_");
                slot.is_rot[180] = cp_model_.NewBoolVar().WithName(prefix + "r180_");
                slot.is_rot[270] = cp_model_.NewBoolVar().WithName(prefix + "r270_");

                slot.bx = cp_model_.NewIntVar(Domain(0, W_a)).WithName(prefix + "bx_");
                slot.by = cp_model_.NewIntVar(Domain(0, H_a)).WithName(prefix + "by_");
                slot.bw = cp_model_.NewIntVar(Domain(0, W_a)).WithName(prefix + "bw_");
                slot.bh = cp_model_.NewIntVar(Domain(0, H_a)).WithName(prefix + "bh_");

                area_slots_[a].push_back(slot);
            }
        }
    }
}

void Solver::declare_constraints()
{
    declare_production_constraints();
    declare_area_constraints();
    declare_power_constraints();
}

void Solver::declare_production_constraints()
{
    for (const auto &[facility, limit] : region_.facility_limits)
    {
        LinearExpr usage;
        int64_t installed = 0;
        for (const auto &area : areas_)
            if (area.area_facilities.count(facility))
                installed += area.area_facilities.at(facility);
        for (size_t p = 0; p < products_.size(); ++p)
        {
            const auto found = products_[p].factory_facilities.find(facility);
            if (found != products_[p].factory_facilities.end())
                for (const auto count : factories_in_area_[p])
                    usage += LinearExpr::Term(count, static_cast<int64_t>(found->second));
        }
        cp_model_.AddLessOrEqual(usage, limit - installed);
    }
    // Build the fuel map for easier lookup
    std::map<std::string, size_t> fuel_map;
    for (size_t fu = 0; fu < fuels_.size(); ++fu)
    {
        fuel_map[fuels_[fu].name] = fu;
    }

    std::map<std::string, LinearExpr> production_by_item;
    std::map<std::string, LinearExpr> rebuilding_by_item;
    std::map<std::string, double> unit_value;
    for (size_t p = 0; p < products_.size(); ++p)
    {
        const auto &product = products_[p];
        if (product.rebuilding)
            rebuilding_by_item[product.name] += qty_produced_[p];
        else
        {
            production_by_item[product.name] += qty_produced_[p];
            unit_value[product.name] = product.value;
        }
    }
    for (const auto &[item, rate] : region_.rebuilding_orders)
        cp_model_.AddEquality(rebuilding_by_item[item], static_cast<int64_t>(rate));

    // Objective: Maximize total net value (sold products)
    LinearExpr objective;

    for (size_t p = 0; p < products_.size(); ++p)
    {
        objective += LinearExpr::Term(
            qty_produced_[p],
            static_cast<int64_t>(std::llround(obj_scale_factor_ * products_[p].value)));
    }

    // Fuel Balance Constraint for ALL fuels
    for (size_t fu = 0; fu < fuels_.size(); ++fu)
    {
        const std::string &fuel_name = fuels_[fu].name;
        if (production_by_item.count(fuel_name))
        {
            int64_t d_fu = static_cast<int64_t>(fuels_[fu].duration);
            int64_t cost_coeff_fu = static_cast<int64_t>(
                std::llround((obj_scale_factor_ * 60 / d_fu) * unit_value.at(fuel_name)));
            objective += LinearExpr::Term(num_batteries_active_[fu], -cost_coeff_fu);

            LinearExpr c_fuel_bal;
            c_fuel_bal += production_by_item.at(fuel_name) * d_fu;
            c_fuel_bal += LinearExpr::Term(num_batteries_active_[fu], -60);
            cp_model_.AddGreaterOrEqual(c_fuel_bal, 0);
        }
        else
        {
            // Fuel is not produced at all: cannot have active batteries!
            cp_model_.AddEquality(num_batteries_active_[fu], 0);
        }
    }
    cp_model_.Maximize(objective);

    // Storage Capacity Constraint
    for (const auto &[item, produced] : production_by_item)
    {
        if (fuel_map.count(item))
        {
            size_t fu = fuel_map.at(item);
            int64_t d_fu = static_cast<int64_t>(fuels_[fu].duration);

            LinearExpr c_storage_capa;
            c_storage_capa += produced * d_fu;
            c_storage_capa += LinearExpr::Term(num_batteries_active_[fu], -60);
            int64_t max_storage =
                static_cast<int64_t>((region_.storage / (inventory_horizon_hours * 60.0)) * d_fu);
            cp_model_.AddLessOrEqual(c_storage_capa, max_storage);
        }
        else
        {
            int64_t max_storage =
                static_cast<int64_t>(region_.storage / (inventory_horizon_hours * 60.0));
            cp_model_.AddLessOrEqual(produced, max_storage);
        }
    }

    // Mineral limits
    for (size_t m = 0; m < mineral_limits_.size(); ++m)
    {
        const std::string &mineral_name = mineral_limits_[m].name;
        int64_t mineral_limit =
            static_cast<int64_t>(std::floor(mineral_limits_[m].limit * 1000000));

        LinearExpr c_mineral;
        for (size_t p = 0; p < products_.size(); ++p)
        {
            if (products_[p].mineral_consumption.count(mineral_name))
            {
                c_mineral += LinearExpr::Term(
                    qty_produced_[p],
                    static_cast<int64_t>(std::ceil(
                        products_[p].mineral_consumption.at(mineral_name) * 1000000 - 1e-7)));
            }
        }
        cp_model_.AddLessOrEqual(c_mineral, mineral_limit);
    }

    // Factory Capacity Constraint
    for (size_t p = 0; p < products_.size(); ++p)
    {
        LinearExpr c_nb_factory;
        c_nb_factory += LinearExpr::Term(
            qty_produced_[p],
            static_cast<int64_t>(std::ceil(products_[p].production_time * 1000000 - 1e-7)));
        for (size_t a = 0; a < areas_.size(); ++a)
        {
            c_nb_factory += LinearExpr::Term(factories_in_area_[p][a], -60000000);
        }
        cp_model_.AddLessOrEqual(c_nb_factory, 0);
    }
}

void Solver::declare_area_constraints()
{
    for (size_t a = 0; a < areas_.size(); ++a)
    {
        if (areas_[a].pac_width * areas_[a].pac_height <= 0 || area_slots_[a].empty())
        {
            for (size_t p = 0; p < products_.size(); ++p)
                cp_model_.AddEquality(factories_in_area_[p][a], 0);
            continue;
        }
        int W_a = areas_[a].pac_width;
        int H_a = areas_[a].pac_height;
        const auto eligible = [&](const Product &product)
        {
            const auto &layout = product_layouts_.at(product.layout_key());
            return (product.candidate_area.empty() || product.candidate_area == areas_[a].name) &&
                   (layout.fixed_area.empty() || layout.fixed_area == areas_[a].name);
        };
        const bool only_anchored =
            std::none_of(products_.begin(), products_.end(),
                         [&](const Product &product) {
                             return eligible(product) &&
                                    product_layouts_.at(product.layout_key()).fixed_area.empty();
                         });
        const auto interferes = [](const BlueprintLayout &a, const BlueprintLayout &b)
        {
            for (const auto &target : a.needed_climates)
                for (const auto &zone : b.emitted_climates)
                    if (target.environment != zone.environment && target.x < zone.x + zone.width &&
                        zone.x < target.x + target.width && target.y < zone.y + zone.height &&
                        zone.y < target.y + target.height)
                        return true;
            return false;
        };
        for (size_t p = 0; p < products_.size(); ++p)
            if (eligible(products_[p]))
            {
                const auto &left = product_layouts_.at(products_[p].layout_key());
                for (size_t q = p + 1; q < products_.size(); ++q)
                    if (eligible(products_[q]))
                    {
                        const auto &right = product_layouts_.at(products_[q].layout_key());
                        if (interferes(left, right) || interferes(right, left))
                            cp_model_.AddLessOrEqual(
                                factories_in_area_[p][a] + factories_in_area_[q][a], 1);
                    }
            }

        bool has_top_depot = false;
        int top_x1 = 0, top_x2 = W_a - 1;
        bool has_right_depot = false;
        int right_y1 = 0, right_y2 = H_a - 1;
        bool has_bottom_depot = false;
        int bot_x1 = 0, bot_x2 = W_a - 1;
        bool has_left_depot = false;
        int left_y1 = 0, left_y2 = H_a - 1;

        for (const auto &d : areas_[a].static_depots)
        {
            if (d.y1 == H_a && d.y2 == H_a)
            {
                has_top_depot = true;
                top_x1 = std::min(d.x1, d.x2);
                top_x2 = std::max(d.x1, d.x2);
            }
            if (d.x1 == W_a && d.x2 == W_a)
            {
                has_right_depot = true;
                right_y1 = std::min(d.y1, d.y2);
                right_y2 = std::max(d.y1, d.y2);
            }
            if (d.y1 == 0 && d.y2 == 0)
            {
                has_bottom_depot = true;
                bot_x1 = std::min(d.x1, d.x2);
                bot_x2 = std::max(d.x1, d.x2);
            }
            if (d.x1 == 0 && d.x2 == 0)
            {
                has_left_depot = true;
                left_y1 = std::min(d.y1, d.y2);
                left_y2 = std::max(d.y1, d.y2);
            }
        }

        std::vector<IntervalVar> x_intervals;
        std::vector<IntervalVar> y_intervals;
        std::vector<IntVar> slot_types;

        for (size_t k = 0; k < area_slots_[a].size(); ++k)
        {
            auto &slot = area_slots_[a][k];
            cp_model_.AddEquality(LinearExpr::Sum(slot.is_prod), slot.active);

            LinearExpr rot_sum;
            rot_sum += slot.is_rot[0];
            rot_sum += slot.is_rot[90];
            rot_sum += slot.is_rot[180];
            rot_sum += slot.is_rot[270];
            cp_model_.AddEquality(rot_sum, slot.active);
            // Slots are interchangeable. Sort product/orientation groups,
            // then their edge coordinate, to remove equivalent permutations.
            LinearExpr type_expression;
            for (size_t p = 0; p < products_.size(); ++p)
                type_expression += LinearExpr::Term(slot.is_prod[p], 4 * p);
            for (int rotation : {0, 90, 180, 270})
                type_expression += LinearExpr::Term(slot.is_rot[rotation], rotation / 90);
            type_expression += LinearExpr::Term(slot.active.Not(), 4 * products_.size());
            auto type = cp_model_.NewIntVar(Domain(0, 4 * products_.size()));
            cp_model_.AddEquality(type, type_expression);
            if (!slot_types.empty())
            {
                cp_model_.AddLessOrEqual(slot_types.back(), type);
                auto same = cp_model_.NewBoolVar();
                cp_model_.AddEquality(slot_types.back(), type).OnlyEnforceIf(same);
                cp_model_.AddNotEqual(slot_types.back(), type).OnlyEnforceIf(same.Not());
                const auto &previous = area_slots_[a][k - 1];
                for (int rotation : {0, 90, 180, 270})
                {
                    const auto current_position =
                        rotation == 0 || rotation == 180 ? slot.bx : slot.by;
                    const auto previous_position =
                        rotation == 0 || rotation == 180 ? previous.bx : previous.by;
                    cp_model_.AddLessOrEqual(previous_position, current_position)
                        .OnlyEnforceIf({same, slot.active, slot.is_rot[rotation]});
                }
            }
            slot_types.push_back(type);
            cp_model_.AddEquality(slot.bx, 0).OnlyEnforceIf(slot.active.Not());
            cp_model_.AddEquality(slot.by, 0).OnlyEnforceIf(slot.active.Not());

            LinearExpr bw_expr;
            LinearExpr bh_expr;
            for (size_t p = 0; p < products_.size(); ++p)
            {
                const auto &product = products_[p];
                const auto &layout = product_layouts_.at(product.layout_key());
                if (!eligible(product))
                    cp_model_.AddEquality(slot.is_prod[p], 0);
                if (!layout.fixed_area.empty())
                {
                    cp_model_.AddEquality(slot.bx, layout.area_origin.first)
                        .OnlyEnforceIf(slot.is_prod[p]);
                    cp_model_.AddEquality(slot.by, layout.area_origin.second)
                        .OnlyEnforceIf(slot.is_prod[p]);
                    cp_model_.AddEquality(slot.is_rot[0], 1).OnlyEnforceIf(slot.is_prod[p]);
                }
                int bw0 = layout.bounding_width;
                int bh0 = layout.bounding_height;

                for (int r : {0, 90, 180, 270})
                {
                    int cur_w = (r == 0 || r == 180) ? bw0 : bh0;
                    int cur_h = (r == 0 || r == 180) ? bh0 : bw0;

                    BoolVar is_pr = cp_model_.NewBoolVar();
                    cp_model_.AddBoolAnd({slot.is_prod[p], slot.is_rot[r]}).OnlyEnforceIf(is_pr);
                    cp_model_.AddBoolOr({slot.is_prod[p].Not(), slot.is_rot[r].Not()})
                        .OnlyEnforceIf(is_pr.Not());

                    bw_expr += LinearExpr::Term(is_pr, cur_w);
                    bh_expr += LinearExpr::Term(is_pr, cur_h);
                    if (!layout.fixed_area.empty() || layout.depot_length == 0)
                        continue;
                    if (r == 0 && has_top_depot)
                    {
                        cp_model_.AddEquality(slot.by + cur_h, H_a).OnlyEnforceIf(is_pr);
                        cp_model_.AddGreaterOrEqual(slot.bx, top_x1).OnlyEnforceIf(is_pr);
                        cp_model_.AddLessOrEqual(slot.bx + cur_w - 1, top_x2).OnlyEnforceIf(is_pr);
                    }
                    else if (r == 90 && has_right_depot)
                    {
                        cp_model_.AddEquality(slot.bx + cur_w, W_a).OnlyEnforceIf(is_pr);
                        cp_model_.AddGreaterOrEqual(slot.by, right_y1).OnlyEnforceIf(is_pr);
                        cp_model_.AddLessOrEqual(slot.by + cur_h - 1, right_y2)
                            .OnlyEnforceIf(is_pr);
                    }
                    else if (r == 180 && has_bottom_depot)
                    {
                        cp_model_.AddEquality(slot.by, 0).OnlyEnforceIf(is_pr);
                        cp_model_.AddGreaterOrEqual(slot.bx, bot_x1).OnlyEnforceIf(is_pr);
                        cp_model_.AddLessOrEqual(slot.bx + cur_w - 1, bot_x2).OnlyEnforceIf(is_pr);
                    }
                    else if (r == 270 && has_left_depot)
                    {
                        cp_model_.AddEquality(slot.bx, 0).OnlyEnforceIf(is_pr);
                        cp_model_.AddGreaterOrEqual(slot.by, left_y1).OnlyEnforceIf(is_pr);
                        cp_model_.AddLessOrEqual(slot.by + cur_h - 1, left_y2).OnlyEnforceIf(is_pr);
                    }
                    else
                        cp_model_.AddEquality(is_pr, 0);
                }
            }
            cp_model_.AddEquality(slot.bw, bw_expr);
            cp_model_.AddEquality(slot.bh, bh_expr);

            cp_model_.AddLessOrEqual(slot.bx + slot.bw, W_a);
            cp_model_.AddLessOrEqual(slot.by + slot.bh, H_a);

            IntVar end_x = cp_model_.NewIntVar(Domain(0, W_a));
            IntVar end_y = cp_model_.NewIntVar(Domain(0, H_a));
            cp_model_.AddEquality(end_x, slot.bx + slot.bw);
            cp_model_.AddEquality(end_y, slot.by + slot.bh);
            IntervalVar ix = cp_model_.NewOptionalIntervalVar(slot.bx, slot.bw, end_x, slot.active);
            IntervalVar iy = cp_model_.NewOptionalIntervalVar(slot.by, slot.bh, end_y, slot.active);
            x_intervals.push_back(ix);
            y_intervals.push_back(iy);

            if (k + 1 < area_slots_[a].size())
            {
                cp_model_.AddGreaterOrEqual(slot.active, area_slots_[a][k + 1].active);
            }
        }

        if (only_anchored)
        {
            std::map<std::pair<int, int>, std::vector<IntVar>> tile_users;
            for (size_t p = 0; p < products_.size(); ++p)
            {
                const auto &product = products_[p];
                if (!eligible(product))
                    continue;
                const auto &layout = product_layouts_.at(product.layout_key());
                std::set<std::pair<int, int>> occupied;
                for (const auto &entity : layout.entities)
                    for (int x = entity.x; x < entity.x + entity.width; ++x)
                        for (int y = entity.y; y < entity.y + entity.height; ++y)
                            occupied.emplace(x, y);
                for (const auto &[tile, directions] : layout.conveyor_cells)
                    occupied.insert(tile);
                for (const auto &[x, y] : occupied)
                    tile_users[{x + layout.area_origin.first, y + layout.area_origin.second}]
                        .push_back(factories_in_area_[p][a]);
            }
            for (const auto &[tile, users] : tile_users)
                if (users.size() > 1)
                    cp_model_.AddLessOrEqual(LinearExpr::Sum(users), 1);
        }
        else
        {
            NoOverlap2DConstraint no_overlap = cp_model_.AddNoOverlap2D();
            for (size_t i = 0; i < x_intervals.size(); ++i)
                no_overlap.AddRectangle(x_intervals[i], y_intervals[i]);
            for (const auto &piece : areas_[a].depot_bus)
                no_overlap.AddRectangle(
                    cp_model_.NewFixedSizeIntervalVar(LinearExpr(piece.x), piece.width),
                    cp_model_.NewFixedSizeIntervalVar(LinearExpr(piece.y), piece.height));
        }

        for (size_t p = 0; p < products_.size(); ++p)
        {
            LinearExpr total_p;
            for (size_t k = 0; k < area_slots_[a].size(); ++k)
            {
                total_p += area_slots_[a][k].is_prod[p];
            }
            cp_model_.AddEquality(factories_in_area_[p][a], total_p);
        }
    }
}

void Solver::declare_power_constraints()
{
    int64_t static_demand = 0;
    for (const auto &area : areas_)
        static_demand +=
            static_cast<int64_t>(facility_power_consumption(area.area_facilities, facility_power_));

    LinearExpr c_power_cons;
    for (size_t p = 0; p < products_.size(); ++p)
    {
        const auto factory_power = static_cast<int64_t>(
            facility_power_consumption(products_[p].factory_facilities, facility_power_));
        if (factory_power > 0)
        {
            for (size_t a = 0; a < areas_.size(); ++a)
                c_power_cons += LinearExpr::Term(factories_in_area_[p][a], factory_power);
        }
    }
    for (size_t fu = 0; fu < fuels_.size(); ++fu)
    {
        c_power_cons +=
            LinearExpr::Term(num_batteries_active_[fu], -static_cast<int64_t>(fuels_[fu].power));
    }

    int64_t max_power_allowed = static_cast<int64_t>(region_.base_power) - static_demand;
    cp_model_.AddLessOrEqual(c_power_cons, max_power_allowed);
}

bool Solver::solve_model()
{
    SatParameters parameters;
    parameters.set_log_search_progress(false);
    parameters.set_num_workers(8);
    parameters.set_max_time_in_seconds(time_limit_seconds_);

    Model model;
    model.Add(NewSatParameters(parameters));

    response_ = SolveCpModel(cp_model_.Build(), &model);
    return response_.status() == CpSolverStatus::OPTIMAL ||
           response_.status() == CpSolverStatus::FEASIBLE;
}

double Solver::objective_value() const
{
    return response_.objective_value() / static_cast<double>(obj_scale_factor_);
}

std::string Solver::status() const
{
    return CpSolverStatus_Name(response_.status());
}

} // namespace aic
