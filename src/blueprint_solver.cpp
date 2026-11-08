#include "blueprint_solver.hpp"
#include "conveyor_router.hpp"
#include "logging.hpp"
#include "ortools/sat/cp_model.h"
#include "plant_module.hpp"
#include "production_network.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>

namespace aic
{

using namespace operations_research;
using namespace operations_research::sat;

BlueprintSolver::BlueprintSolver(const std::vector<Facility> &facilities,
                                 const std::vector<Mineral> &minerals,
                                 const std::vector<Area> &areas, BlueprintOptions options)
    : graph_(facilities, minerals, options.automatic_recipes), options_(options), areas_(areas)
{
    if (!std::isfinite(options.placement_seconds) || options.placement_seconds < 0 ||
        options.max_attempts < 1)
        throw std::invalid_argument("Invalid blueprint search options");
}

bool BlueprintSolver::is_mineral(const std::string &item) const
{
    return graph_.is_resource(item);
}

bool BlueprintSolver::solve(Product &product, BlueprintLayout &layout)
{
    layout = BlueprintLayout{};
    status_ = BlueprintStatus::Infeasible;
    const auto plan = graph_.plan_target(product.name);
    layout.output_per_minute = plan.target_rate * 60;
    layout.startup_items = plan.startup_items;
    layout.startup_stock = plan.startup_stock;
    product.production_time = 1.0 / plan.target_rate;
    product.mineral_consumption.clear();
    product.factory_facilities.clear();
    for (const auto &[item, rate] : plan.external_rates)
        product.mineral_consumption[item] = rate / plan.target_rate;
    for (const auto &step : plan.steps)
        product.factory_facilities[step.facility->name] += step.instances;
    auto network = build_factory_network(plan, product.name, graph_.facility("protocol_stash"),
                                         graph_.facility("unloader"));
    auto &entities = network.entities;
    auto &streams = network.streams;
    const auto module_pylons = add_plant_pylons(network, graph_);
    for (const auto &[picker, pole] : module_pylons)
        ++product.factory_facilities[entities[pole].type];
    const bool standalone_plant =
        module_pylons.size() == 1 && plan.steps.size() == 3 && plan.external_rates.empty() &&
        entities[entities[module_pylons.begin()->first].loop_harvest].item_produced == product.name;
    const int root_entity_id = network.root_entity_id;
    const int stash_entity_id = network.stash_entity_id;
    const int total_unloaders = network.total_unloaders;
    const int unloader_width = network.unloader_width;
    product.factory_facilities["protocol_stash"] = 1;
    product.factory_facilities["unloader"] = total_unloaders;
    for (const auto &item : plan.startup_items)
        log(LogLevel::Info, product.name, ": initialize recycling with ",
            plan.startup_stock.at(item), " x ", item, " in the seed-picking units (once)");

    // Determine grid bounds from area
    int grid_w = 30;
    int grid_h = 30;
    if (!areas_.empty())
    {
        if (areas_[0].pac_width > 0)
            grid_w = static_cast<int>(areas_[0].pac_width);
        if (areas_[0].pac_height > 0)
            grid_h = static_cast<int>(areas_[0].pac_height);
    }

    const bool complex_network =
        !plan.startup_items.empty() ||
        std::any_of(plan.steps.begin(), plan.steps.end(),
                    [&](const ProductionStep &step)
                    {
                        return step.recipe->products.front().item != product.name &&
                               step.recipe->ingredients.size() > 1;
                    });

    // Build CP-SAT Model
    CpModelBuilder model;
    int N = entities.size();

    std::vector<IntVar> posX, posY, effW, effH;
    std::vector<std::vector<BoolVar>> rotB;
    std::vector<IntervalVar> intX, intY;

    for (int i = 0; i < N; ++i)
    {
        posX.push_back(model.NewIntVar(Domain(0, grid_w - 1)).WithName(entities[i].name + "x_"));
        posY.push_back(model.NewIntVar(Domain(0, grid_h - 1)).WithName(entities[i].name + "y_"));

        std::vector<BoolVar> b;
        for (int r = 0; r < 4; ++r)
        {
            b.push_back(
                model.NewBoolVar().WithName(entities[i].name + "rot__" + std::to_string(r)));
        }
        rotB.push_back(b);

        // Stable north-input/south-output banks leave predictable routing lanes in
        // recycling networks; simple chains retain the full rotation search.
        if (complex_network && !entities[i].is_unloader)
        {
            const bool upright =
                entities[i].loop_role == SeedLoopRole::Picker && entities[i].loop_harvest >= 0;
            const int rotation =
                standalone_plant && i == stash_entity_id
                    ? 3
                    : (upright || entities[i].loop_role == SeedLoopRole::Pylon ? 0 : 2);
            model.AddEquality(b[rotation], 1);
            if (!standalone_plant)
            {
                model.AddGreaterOrEqual(posX[i], 2);
                model.AddGreaterOrEqual(posY[i], 2);
            }
        }

        LinearExpr rot_sum;
        for (const auto &bv : b)
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

        LinearExpr expr_w = LinearExpr::Term(b[0], bw) + LinearExpr::Term(b[2], bw) +
                            LinearExpr::Term(b[1], bh) + LinearExpr::Term(b[3], bh);
        LinearExpr expr_h = LinearExpr::Term(b[0], bh) + LinearExpr::Term(b[2], bh) +
                            LinearExpr::Term(b[1], bw) + LinearExpr::Term(b[3], bw);

        model.AddEquality(w, expr_w);
        model.AddEquality(h, expr_h);

        effW.push_back(w);
        effH.push_back(h);
        if (complex_network && !entities[i].is_unloader && !standalone_plant)
        {
            model.AddLessOrEqual(posX[i] + w, grid_w - 2);
            model.AddLessOrEqual(posY[i] + h, grid_h - 4);
        }

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
            model.AddEquality(posX[u_idx], LinearExpr::Term(unloader_slots[k], unloader_width));
        }
    }

    // Non-overlapping
    NoOverlap2DConstraint no_overlap = model.AddNoOverlap2D();
    for (size_t i = 0; i < intX.size(); ++i)
    {
        no_overlap.AddRectangle(intX[i], intY[i]);
    }

    if (complex_network)
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
            {
                if (entities[i].is_unloader || entities[j].is_unloader)
                    continue;
                if (!entities[i].seed_loop.empty() &&
                    entities[i].seed_loop == entities[j].seed_loop &&
                    (entities[i].loop_harvest >= 0 ||
                     entities[i].loop_role == SeedLoopRole::Pylon ||
                     entities[j].loop_role == SeedLoopRole::Pylon))
                    continue;
                if (standalone_plant)
                    continue;
                const bool both_primary =
                    is_mineral(entities[i].item_consumed) && is_mineral(entities[j].item_consumed);
                const int horizontal_gap = both_primary ? 0 : 2;
                const int vertical_gap = 3;
                const auto left = model.NewBoolVar(), right = model.NewBoolVar();
                const auto below = model.NewBoolVar(), above = model.NewBoolVar();
                model.AddLessOrEqual(posX[i] + effW[i] + horizontal_gap, posX[j])
                    .OnlyEnforceIf(left);
                model.AddLessOrEqual(posX[j] + effW[j] + horizontal_gap, posX[i])
                    .OnlyEnforceIf(right);
                model.AddLessOrEqual(posY[i] + effH[i] + vertical_gap, posY[j])
                    .OnlyEnforceIf(below);
                model.AddLessOrEqual(posY[j] + effH[j] + vertical_gap, posY[i])
                    .OnlyEnforceIf(above);
                model.AddBoolOr({left, right, below, above});
            }

    // Keep each autonomous seed module together; no boundary/depot alignment is needed.
    for (const auto &entity : entities)
        if (entity.loop_role == SeedLoopRole::Picker)
        {
            const int partner = entity.loop_partner;
            model.AddEquality(posY[entity.id], posY[partner]);
            if (entity.loop_harvest >= 0)
            {
                const int harvest = entity.loop_harvest;
                const int pole = module_pylons.at(entity.id);
                model.AddEquality(posX[entity.id], posX[partner] + 5);
                model.AddEquality(posX[harvest], posX[partner] + 10);
                model.AddEquality(posY[harvest], posY[partner]);
                model.AddEquality(posX[pole], posX[partner] + 9);
                model.AddEquality(posY[pole], posY[partner] - 3);
                // Reserve the entire module envelope so unrelated machines cannot
                // occupy the top loops or the export lane above the pylon.
                for (int other = 0; other < N; ++other)
                {
                    if (entities[other].seed_loop == entity.seed_loop ||
                        (standalone_plant && other == stash_entity_id))
                        continue;
                    const auto left = model.NewBoolVar(), right = model.NewBoolVar();
                    const auto below = model.NewBoolVar(), above = model.NewBoolVar();
                    model.AddLessOrEqual(posX[other] + effW[other], posX[partner])
                        .OnlyEnforceIf(left);
                    model.AddLessOrEqual(posX[partner] + plant_module_width, posX[other])
                        .OnlyEnforceIf(right);
                    model.AddLessOrEqual(posY[other] + effH[other], posY[partner] - 3)
                        .OnlyEnforceIf(below);
                    model.AddLessOrEqual(posY[partner] + 6, posY[other]).OnlyEnforceIf(above);
                    model.AddBoolOr({left, right, below, above});
                }
                if (standalone_plant)
                {
                    model.AddEquality(posX[partner], 0);
                    model.AddEquality(posY[partner], 3);
                    model.AddEquality(posX[stash_entity_id], posX[partner] + 6);
                    model.AddEquality(posY[stash_entity_id], posY[partner] - 3);
                    for (auto &stream : streams)
                        if (stream.dst_entity == stash_entity_id)
                            stream.dst_port_idx = 2;
                }
                continue;
            }
            auto picker_left = model.NewBoolVar();
            model.AddEquality(posX[entity.id] + effW[entity.id] + 4, posX[partner])
                .OnlyEnforceIf(picker_left);
            model.AddEquality(posX[partner] + effW[partner] + 4, posX[entity.id])
                .OnlyEnforceIf(picker_left.Not());
        }

    // Topological Tier 1: Primary mineral consumers form a contiguous row under unloaders
    std::vector<int> primary_consumer_ids;
    int primary_total_w = 0;
    for (const auto &ent : entities)
    {
        if (!ent.is_unloader && ent.type != "protocol_stash" && ent.id != root_entity_id)
        {
            if (ent.loop_role == SeedLoopRole::None && is_mineral(ent.item_consumed))
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
            model.AddEquality(rotB[cid][2],
                              1); // rot 180: inputs North (facing unloaders), outputs South
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
        const auto &ent = entities[i];
        int bw = ent.base_w;
        int bh = ent.base_h;
        const auto &b = rotB[i];

        auto add_ports = [&](const std::vector<Port> &ports, bool is_input)
        {
            for (size_t p = 0; p < ports.size(); ++p)
            {
                const bool used =
                    std::any_of(streams.begin(), streams.end(),
                                [&](const StreamLink &stream)
                                {
                                    return is_input
                                               ? stream.dst_entity == i &&
                                                     stream.dst_port_idx == static_cast<int>(p)
                                               : stream.src_entity == i &&
                                                     stream.src_port_idx == static_cast<int>(p);
                                });
                if (!used)
                    continue;
                const auto &port = ports[p];
                LinearExpr px, py, fx, fy;
                for (int rotation = 0; rotation < 4; ++rotation)
                {
                    const auto placed =
                        rotate_port({port.x, port.y, is_input, port.fx, port.fy, port.type, ""}, bw,
                                    bh, rotation * 90);
                    px += LinearExpr::Term(b[rotation], placed.x);
                    py += LinearExpr::Term(b[rotation], placed.y);
                    fx += LinearExpr::Term(b[rotation], placed.fx);
                    fy += LinearExpr::Term(b[rotation], placed.fy);
                }

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
                    model.AddGreaterOrEqual(ax, posX[j] + effW[j]).OnlyEnforceIf(right);
                    model.AddLessOrEqual(ay, posY[j] - 1).OnlyEnforceIf(below);
                    model.AddGreaterOrEqual(ay, posY[j] + effH[j]).OnlyEnforceIf(above);

                    model.AddBoolOr({left, right, below, above});
                }

                // For multi-input facilities (e.g. packaging_unit), also ensure the 2nd tile in
                // front is clear
                if (is_input && ent.inputs.size() > 2 && ent.loop_harvest < 0 && !standalone_plant)
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
                        model.AddGreaterOrEqual(ax2, posX[j] + effW[j]).OnlyEnforceIf(right2);
                        model.AddLessOrEqual(ay2, posY[j] - 1).OnlyEnforceIf(below2);
                        model.AddGreaterOrEqual(ay2, posY[j] + effH[j]).OnlyEnforceIf(above2);

                        model.AddBoolOr({left2, right2, below2, above2});
                    }
                }

                port_accesses.push_back({i, is_input, static_cast<int>(p), ax, ay});
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
    for (const auto &ent : entities)
        total_fac_area += ent.base_w * ent.base_h;
    int max_dim =
        std::min(grid_w, std::max(total_unloaders * unloader_width + 8,
                                  static_cast<int>(std::ceil(std::sqrt(total_fac_area) * 2.3))));
    if (!complex_network && max_dim < grid_w)
    {
        model.AddLessOrEqual(bbox_w, max_dim);
        model.AddLessOrEqual(bbox_h, max_dim);
    }

    // Minimize Manhattan conveyor distances as regularization
    auto find_access = [&](int ent_id, bool is_in, int p_idx) -> const PortAccessVar *
    {
        for (const auto &pa : port_accesses)
        {
            if (pa.entity_id == ent_id && pa.is_input == is_in && pa.port_idx == p_idx)
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
        const auto &s = streams[s_idx];
        const auto *src_pa = find_access(s.src_entity, false, s.src_port_idx);
        const auto *dst_pa = find_access(s.dst_entity, true, s.dst_port_idx);
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

    if (complex_network && !standalone_plant)
    {
        // Feed CP-SAT a spacious shelf layout ordered by material dependency.
        // It remains a hint: all placement and access constraints are still checked.
        std::function<int(const std::string &, std::set<std::string>)> depth;
        depth = [&](const std::string &item, std::set<std::string> path) -> int
        {
            if (is_mineral(item) || !path.insert(item).second)
                return 0;
            int value = 0;
            for (const auto &step : plan.steps)
                if (step.recipe->products.front().item == item)
                    for (const auto &ingredient : step.recipe->ingredients)
                        value = std::max(value, depth(ingredient.item, path));
            return value + 1;
        };
        std::vector<std::pair<int, int>> hints(N, {-1, -1});
        const int primary_height =
            primary_consumer_ids.empty() ? 0 : entities[primary_consumer_ids.front()].base_h;
        const int primary_y = grid_h - 4 - primary_height;
        int primary_x = 2;
        for (const int index : primary_consumer_ids)
        {
            hints[index] = {primary_x, primary_y};
            primary_x += entities[index].base_w;
        }
        std::vector<int> order;
        for (int index = 0; index < N; ++index)
            if (!entities[index].is_unloader &&
                std::find(primary_consumer_ids.begin(), primary_consumer_ids.end(), index) ==
                    primary_consumer_ids.end())
                order.push_back(index);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b)
                         {
                             if (a == stash_entity_id || b == stash_entity_id)
                                 return b == stash_entity_id && a != stash_entity_id;
                             return depth(entities[a].item_produced, {}) <
                                    depth(entities[b].item_produced, {});
                         });
        std::vector<int> grouped;
        std::set<int> grouped_ids;
        for (const int index : order)
        {
            if (grouped_ids.count(index))
                continue;
            const auto &entity = entities[index];
            if (entity.loop_role != SeedLoopRole::None)
            {
                const int picker =
                    entity.loop_role == SeedLoopRole::Picker ? index : entity.loop_partner;
                const int planter = entities[picker].loop_partner;
                grouped.push_back(picker);
                grouped.push_back(planter);
                grouped_ids.insert(picker);
                grouped_ids.insert(planter);
                if (entities[picker].loop_harvest >= 0)
                {
                    const int harvest = entities[picker].loop_harvest,
                              pole = module_pylons.at(picker);
                    grouped.push_back(harvest);
                    grouped.push_back(pole);
                    grouped_ids.insert(harvest);
                    grouped_ids.insert(pole);
                }
            }
            else
            {
                grouped.push_back(index);
                grouped_ids.insert(index);
            }
        }
        order = std::move(grouped);
        int x = 2, top = primary_consumer_ids.empty() ? grid_h - 4 : primary_y - 4, row_height = 0;
        for (const int index : order)
        {
            if (hints[index].first >= 0)
                continue;
            const auto &entity = entities[index];
            if (entity.loop_role == SeedLoopRole::Picker && entity.loop_harvest >= 0)
            {
                if (x + plant_module_width > grid_w - 2 && row_height > 0)
                {
                    x = 2;
                    top -= row_height + 4;
                    row_height = 0;
                }
                hints[entity.loop_partner] = {x, top - 5};
                hints[index] = {x + 5, top - 5};
                hints[entity.loop_harvest] = {x + 10, top - 5};
                hints[module_pylons.at(index)] = {x + 9, top - 8};
                x += plant_module_width + 4;
                row_height = std::max(row_height, plant_module_height);
                continue;
            }
            const int group_width = entity.loop_role == SeedLoopRole::Picker
                                        ? entity.base_w + 4 + entities[entity.loop_partner].base_w
                                        : entity.base_w;
            if ((x + group_width > grid_w - 2 || index == stash_entity_id) && row_height > 0)
            {
                x = 2;
                top -= row_height + 4;
                row_height = 0;
            }
            hints[index] = {x, top - entity.base_h};
            x += entity.base_w + 4;
            row_height = std::max(row_height, entity.base_h);
        }
        if (std::all_of(order.begin(), order.end(),
                        [&](int index) { return hints[index].second >= 2; }))
            for (int index = 0; index < N; ++index)
                if (!entities[index].is_unloader)
                {
                    model.AddHint(posX[index], hints[index].first);
                    model.AddHint(posY[index], hints[index].second);
                }
    }

    SatParameters params;
    params.set_max_time_in_seconds(options_.placement_seconds == 0
                                       ? 0
                                       : (complex_network
                                              ? std::max(options_.placement_seconds, 6.0)
                                              : options_.placement_seconds));
    params.set_stop_after_first_solution(complex_network);
    params.set_num_search_workers(8);
    params.set_random_seed(42);

    const int max_conveyor_attempts = options_.max_attempts;

    for (int attempt = 1; attempt <= max_conveyor_attempts; ++attempt)
    {
        params.set_random_seed(42 + attempt * 17);
        Model cp_model;
        cp_model.Add(NewSatParameters(params));
        CpSolverResponse response = SolveCpModel(model.Build(), &cp_model);

        if (response.status() != CpSolverStatus::OPTIMAL &&
            response.status() != CpSolverStatus::FEASIBLE)
        {
            if (response.status() == CpSolverStatus::MODEL_INVALID)
                throw std::runtime_error("Invalid blueprint model: " + response.solution_info());
            status_ = response.status() == CpSolverStatus::INFEASIBLE ? BlueprintStatus::Infeasible
                                                                      : BlueprintStatus::Timeout;
            log(LogLevel::Warning, product.name, ": ",
                status_ == BlueprintStatus::Timeout ? "placement search timed out"
                                                    : "placement model infeasible");
            return false;
        }

        layout.entities.clear();
        std::map<std::string, int> type_instance_count;
        for (int i = 0; i < N; ++i)
        {
            PlacedBlueprintEntity pe;
            pe.name = entities[i].name;
            pe.seed_loop = entities[i].seed_loop;
            if (entities[i].recipe)
            {
                pe.recipe_name = entities[i].recipe->name;
                const auto &output = entities[i].recipe->products.front();
                pe.utilization = entities[i].output_rates.at(output.item) / output.amount *
                                 entities[i].recipe->time;
            }
            pe.type = entities[i].type;
            pe.x = SolutionIntegerValue(response, posX[i]);
            pe.y = SolutionIntegerValue(response, posY[i]);
            pe.width = SolutionIntegerValue(response, effW[i]);
            pe.height = SolutionIntegerValue(response, effH[i]);
            pe.is_unloader = entities[i].is_unloader;
            pe.mineral = entities[i].item_produced;

            int inst_idx = type_instance_count[pe.type]++;
            pe.code = get_facility_code(pe.type, inst_idx);
            pe.color_code = get_facility_color(pe.type);

            int rot = 0;
            for (int r = 0; r < 4; ++r)
            {
                if (SolutionBooleanValue(response, rotB[i][r]))
                {
                    rot = r * 90;
                    break;
                }
            }
            pe.rotation = (rot + entities[i].base_rotation) % 360;

            int bw = entities[i].base_w;
            int bh = entities[i].base_h;
            auto add_placed_port = [&](const Port &p, bool is_in)
            {
                auto placed = rotate_port({p.x, p.y, is_in, p.fx, p.fy, p.type, ""}, bw, bh, rot);
                placed.x += pe.x;
                placed.y += pe.y;
                pe.ports.push_back(placed);
            };

            for (const auto &in_p : entities[i].inputs)
                add_placed_port(in_p, true);
            for (const auto &out_p : entities[i].outputs)
                add_placed_port(out_p, false);

            layout.entities.push_back(pe);
        }

        fix_plant_paths(network, layout, standalone_plant);
        const auto routing = route_conveyors(layout, streams, grid_w, grid_h);
        const auto &conflicted_stream_indices = routing.conflicted_stream_indices;
        if (routing.feasible)
        {
            if (attempt > 1)
            {
                log(LogLevel::Debug, "Routing feasible for ", product.name, " on attempt ",
                    attempt);
            }

            product.factory_width = SolutionIntegerValue(response, bbox_w);
            product.factory_height = SolutionIntegerValue(response, bbox_h);
            product.factory_depot = total_unloaders * unloader_width;

            layout.product_name = product.name;
            layout.bounding_width = static_cast<int>(product.factory_width);
            layout.bounding_height = static_cast<int>(product.factory_height);
            layout.depot_length = static_cast<int>(product.factory_depot);

            layout.normalize();
            product.factory_width = layout.bounding_width;
            product.factory_height = layout.bounding_height;

            status_ = BlueprintStatus::Success;
            return true;
        }

        // Conveyor routing infeasible: Add no-good cuts to CP-SAT
        log(LogLevel::Debug, "Routing conflict for ", product.name, " on attempt ", attempt, ": ",
            conflicted_stream_indices.size(), " conflicted streams; adding placement cuts");

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
            model.AddBoolAnd({match_x, match_y, rotB[i][sol_r]}).OnlyEnforceIf(match_i);
            model.AddBoolOr({match_x.Not(), match_y.Not(), rotB[i][sol_r].Not()})
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
                model.AddBoolOr({entity_matches[u].Not(), entity_matches[v].Not()});

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

                model.AddBoolOr(
                    {match_dx.Not(), match_dy.Not(), rotB[u][ru].Not(), rotB[v][rv].Not()});
            }
        }

        // Pairwise cuts for streams sharing a conflicted cell.
        for (const auto &s_indices : routing.conflict_groups)
        {
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

    status_ = BlueprintStatus::RoutingExhausted;
    log(LogLevel::Warning, product.name, ": routing search exhausted after ", max_conveyor_attempts,
        " placements");
    return false;
}

} // namespace aic
