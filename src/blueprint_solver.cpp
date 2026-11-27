#include "blueprint_solver.hpp"
#include "blueprint_compactor.hpp"
#include "conveyor_router.hpp"
#include "logging.hpp"
#include "ortools/sat/cp_model.h"
#include "plant_module.hpp"
#include "power_coverage.hpp"
#include "production_network.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <queue>
#include <random>
#include <tuple>

namespace aic
{

using namespace operations_research;
using namespace operations_research::sat;

namespace
{
struct LayoutSignature
{
    std::string key;
    std::vector<int> entity_order, stream_order;
};

LayoutSignature layout_signature(const FactoryNetwork &network, const RecipeGraph &graph,
                                 int grid_width, int grid_height, bool complex,
                                 bool standalone_plant)
{
    using nlohmann::json;
    const auto &entities = network.entities;
    const auto &streams = network.streams;
    std::vector<std::vector<std::pair<int, int>>> neighbors(entities.size());
    for (const auto &stream : streams)
    {
        // Visit each incoming port before the outgoing ports, in physical port order.
        neighbors[stream.dst_entity].emplace_back(stream.dst_port_idx, stream.src_entity);
        const int output = entities[stream.src_entity].inputs.size() + stream.src_port_idx;
        neighbors[stream.src_entity].emplace_back(output, stream.dst_entity);
    }
    for (const auto &entity : entities)
        if (entity.loop_role == SeedLoopRole::Pylon)
        {
            const auto &picker = entities[entity.loop_partner];
            neighbors[picker.id].emplace_back(picker.inputs.size() + picker.outputs.size(),
                                              entity.id);
        }

    LayoutSignature signature;
    std::vector<int> entity_ids(entities.size(), -1);
    auto visit = [&](int id)
    {
        if (entity_ids[id] < 0)
        {
            entity_ids[id] = signature.entity_order.size();
            signature.entity_order.push_back(id);
        }
    };
    visit(network.stash_entity_id);
    for (size_t i = 0; i < signature.entity_order.size(); ++i)
    {
        auto &adjacent = neighbors[signature.entity_order[i]];
        std::sort(adjacent.begin(), adjacent.end());
        for (const auto &link : adjacent)
            visit(link.second);
    }
    for (const auto &entity : entities)
        visit(entity.id);

    auto port_geometry = [](const std::vector<Port> &ports)
    {
        auto result = json::array();
        for (const auto &port : ports)
            result.push_back({port.x, port.y, port.fx, port.fy, port.type});
        return result;
    };

    auto machines = json::array();
    std::map<std::string, int> loop_ids;
    auto canonical_id = [&](int id) { return id < 0 ? -1 : entity_ids[id]; };
    for (const int id : signature.entity_order)
    {
        const auto &entity = entities[id];
        const int loop_id =
            entity.seed_loop.empty()
                ? -1
                : loop_ids.emplace(entity.seed_loop, canonical_id(id)).first->second;
        machines.push_back({entity.type, entity.base_w, entity.base_h, entity.base_rotation,
                            entity.is_unloader, entity.needs_power,
                            graph.is_resource(entity.item_consumed), entity.loop_role, loop_id,
                            canonical_id(entity.loop_partner), canonical_id(entity.loop_harvest),
                            port_geometry(entity.inputs), port_geometry(entity.outputs)});
    }

    std::map<std::pair<int, int>, int> stream_by_port;
    for (size_t i = 0; i < streams.size(); ++i)
        stream_by_port.emplace(
            std::make_pair(canonical_id(streams[i].src_entity), streams[i].src_port_idx), i);
    auto links = json::array();
    for (const auto &[port, index] : stream_by_port)
    {
        signature.stream_order.push_back(index);
        const auto &stream = streams[index];
        links.push_back(
            {port.first, port.second, canonical_id(stream.dst_entity), stream.dst_port_idx});
    }
    signature.key =
        json::array({grid_width, grid_height, complex, standalone_plant,
                     canonical_id(network.root_entity_id), network.unloader_width, machines, links})
            .dump();
    return signature;
}

void set_entity_labels(PlacedBlueprintEntity &placed, const FactoryEntity &entity)
{
    placed.name = entity.name;
    placed.seed_loop = entity.seed_loop;
    placed.mineral = entity.item_produced;
    placed.recipe_name.clear();
    placed.operating_mode = entity.projection ? entity.projection->mode : "";
    if (entity.pipe_limit_per_minute > 0)
        placed.operating_mode =
            "flow limit " + std::to_string(std::llround(entity.pipe_limit_per_minute)) + "/min";
    placed.utilization = 0;
    if (entity.recipe)
    {
        placed.recipe_name = entity.recipe->name;
        const auto &output = entity.recipe->products.front();
        placed.utilization =
            entity.output_rates.at(output.item) / output.amount * entity.cycle_seconds;
    }
}

void relabel_layout(BlueprintLayout &layout, const FactoryNetwork &network)
{
    std::map<std::string, int> type_instance_count;
    for (const auto &entity : network.entities)
    {
        auto &placed = layout.entities[entity.id];
        set_entity_labels(placed, entity);
        placed.code = get_facility_code(entity.type, type_instance_count[entity.type]++);
        for (auto &port : placed.ports)
            port.item.clear();
    }

    auto label_port =
        [&](int entity_id, const std::pair<int, int> &cell, bool is_input, const std::string &item)
    {
        for (auto &port : layout.entities[entity_id].ports)
            if (port.x == cell.first && port.y == cell.second && port.is_input == is_input)
            {
                port.item = item;
                break;
            }
    };
    for (size_t i = 0; i < network.streams.size(); ++i)
    {
        const auto &stream = network.streams[i];
        const auto &path = layout.conveyor_paths[i];
        label_port(stream.src_entity, path.front(), false, stream.item);
        label_port(stream.dst_entity, path.back(), true, stream.item);
        for (auto cell = path.begin() + 1; cell != path.end() - 1; ++cell)
            layout.conveyor_items.at(*cell) = stream.item;
    }
}

std::map<int, std::vector<int>> dependency_rows(const FactoryNetwork &network, bool area_bound)
{
    const auto &entities = network.entities;
    const int count = entities.size();
    std::vector<std::vector<int>> successors(count);
    std::vector<int> incoming(count), depth(count, -1), latest(count, -1);
    auto in_rows = [&](int id)
    {
        const auto &entity = entities[id];
        return entity.seed_loop.empty() && id != network.stash_entity_id &&
               (!area_bound ||
                (!entity.site && !entity.array_field && entity.type != "seed_picking_unit"));
    };
    for (const auto &stream : network.streams)
        if (in_rows(stream.src_entity) && in_rows(stream.dst_entity) &&
            (!area_bound || stream.phase == MaterialPhase::Solid))
        {
            successors[stream.src_entity].push_back(stream.dst_entity);
            ++incoming[stream.dst_entity];
        }

    std::vector<int> order;
    int ranked_count = 0;
    for (const auto &entity : entities)
        if (in_rows(entity.id))
        {
            ++ranked_count;
            if (incoming[entity.id] == 0)
                order.push_back(entity.id);
            if (entity.is_unloader)
                depth[entity.id] = 0;
        }
    for (size_t index = 0; index < order.size(); ++index)
    {
        const int id = order[index];
        for (const int next : successors[id])
        {
            if (depth[id] >= 0)
                depth[next] = std::max(depth[next], depth[id] + 1);
            if (--incoming[next] == 0)
                order.push_back(next);
        }
    }

    const int sink = network.root_entity_id;
    if (order.size() != static_cast<size_t>(ranked_count) || depth[sink] < 1)
        return {};

    latest[sink] = depth[sink];
    std::map<int, std::vector<int>> rows;
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        const int id = *it;
        for (const int next : successors[id])
            if (latest[next] >= 0)
                latest[id] =
                    latest[id] < 0 ? latest[next] - 1 : std::min(latest[id], latest[next] - 1);
        if (entities[id].is_unloader)
            rows[0].push_back(id);
        else if (latest[id] >= 0)
        {
            // Keep shorter depot branches closer to assembly and place crop processors
            // as late as their consumers allow.
            const int stage = depth[id] < 0 ? latest[id] : std::min(depth[id] + 1, latest[id]);
            if (stage > 0)
                rows[stage].push_back(id);
        }
    }
    return rows;
}
} // namespace

BlueprintSolver::BlueprintSolver(const std::vector<Facility> &facilities,
                                 const std::vector<Mineral> &minerals,
                                 const std::vector<Area> &areas, BlueprintOptions options)
    : graph_(facilities, minerals, options.automatic_recipes, options.local_area),
      options_(options), areas_(areas)
{
    if (!std::isfinite(options.placement_seconds) || options.placement_seconds < 0 ||
        !std::isfinite(options.compact_seconds) || options.compact_seconds < 0 ||
        options.max_attempts < 1 ||
        (options.blueprint_size &&
         (options.blueprint_size->first <= 0 || options.blueprint_size->second <= 0)))
        throw std::invalid_argument("Invalid blueprint search options");
    const auto id = options.local_area && !options.local_area->power_pylon.empty()
                        ? options.local_area->power_pylon
                        : "power_diffuser_1";
    const auto pylon = std::find_if(facilities.begin(), facilities.end(),
                                    [&](const Facility &facility)
                                    {
                                        return (facility.id == id || facility.name == id) &&
                                               facility.power_range_extension.has_value();
                                    });
    if (pylon != facilities.end())
        electric_pylon_ = &*pylon;
}

bool BlueprintSolver::is_mineral(const std::string &item) const
{
    return graph_.is_resource(item);
}

struct BlueprintSolver::PlacementProblem
{
    const std::string &name;
    const ProductionPlan &plan;
    const std::map<int, int> &module_pylons;
    int grid_w, grid_h;
    bool complex_network, standalone_plant;
    bool anchored;
};

bool BlueprintSolver::solve(Product &product, BlueprintLayout &layout, bool reuse_only)
{
    if (!options_.local_area)
        return solve_plan(product, layout, graph_.plan_target(product.name), reuse_only);
    if (reuse_only)
        return false;
    std::set<const Recipe *> excluded;
    std::string failure;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ProductionPlan plan;
        try
        {
            plan = graph_.plan_target(product.name, excluded);
        }
        catch (const std::invalid_argument &)
        {
            if (excluded.empty())
                throw;
            break;
        }
        try
        {
            if (solve_plan(product, layout, plan))
                return true;
        }
        catch (const std::invalid_argument &error)
        {
            failure = error.what();
        }
        if (!plan.final_recipe)
            break;
        excluded.insert(plan.final_recipe);
    }
    if (!failure.empty())
        throw std::invalid_argument(failure);
    return false;
}

bool BlueprintSolver::solve_plan(Product &product, BlueprintLayout &layout,
                                 const ProductionPlan &plan, bool reuse_only)
{
    layout = BlueprintLayout{};
    status_ = BlueprintStatus::Infeasible;
    const auto &blueprint_name = product.name;
    layout.output_per_minute = plan.target_rate * 60;
    layout.startup_items = plan.startup_items;
    layout.startup_stock = plan.startup_stock;
    product.production_time = 1.0 / plan.target_rate;
    product.mineral_consumption.clear();
    product.factory_facilities.clear();
    for (const auto &[item, rate] : plan.external_rates)
        product.mineral_consumption[item] = rate / plan.target_rate;
    if (product.rebuilding && (!plan.area || !plan.area->rebuilding_port))
        throw std::invalid_argument("Missing areas.json rebuilding_port for " +
                                    (plan.area ? plan.area->name : blueprint_name));
    const auto *delivery = product.rebuilding ? &*plan.area->rebuilding_port : nullptr;
    const bool use_depot_loader = plan.area && plan.area->output_to_depot_bus && !delivery;
    auto network = build_factory_network(
        plan, product.name,
        graph_.facility(delivery ? delivery->facility
                                 : (use_depot_loader ? "loader_1" : "protocol_stash")),
        graph_.facility("unloader"), delivery);
    const auto module_pylons = add_plant_pylons(network, graph_);
    const auto &entities = network.entities;
    for (const auto &entity : entities)
        if (!entity.array_field)
            ++product.factory_facilities[entity.type];
    const bool anchored =
        plan.area &&
        (plan.disperser || plan.area->rebuilding_port || !plan.area->depot_bus.empty() ||
         std::any_of(entities.begin(), entities.end(),
                     [](const FactoryEntity &entity) { return entity.site; }));
    const bool standalone_plant =
        module_pylons.size() == 1 && plan.steps.size() == 3 && plan.external_rates.empty() &&
        entities[entities[module_pylons.begin()->first].loop_harvest].item_produced == product.name;
    if (!reuse_only)
        for (const auto &item : plan.startup_items)
            log(LogLevel::Info, blueprint_name, ": initialize recycling with ",
                plan.startup_stock.at(item), " x ", item, " (once)");

    // Determine grid bounds from area
    int grid_w = 30;
    int grid_h = 30;
    const auto *area = plan.area ? plan.area : (areas_.empty() ? nullptr : &areas_.front());
    if (area)
    {
        if (area->pac_width > 0)
            grid_w = static_cast<int>(area->pac_width);
        if (area->pac_height > 0)
            grid_h = static_cast<int>(area->pac_height);
    }
    if (options_.blueprint_size && !anchored)
    {
        grid_w = std::min(grid_w, options_.blueprint_size->first);
        grid_h = std::min(grid_h, options_.blueprint_size->second);
    }

    const bool complex_network =
        !plan.startup_items.empty() ||
        std::any_of(plan.steps.begin(), plan.steps.end(),
                    [&](const ProductionStep &step)
                    {
                        return step.recipe->products.front().item != product.name &&
                               step.recipe->ingredients.size() > 1;
                    });

    auto finish = [&]
    {
        product.factory_width = layout.bounding_width;
        product.factory_height = layout.bounding_height;
        product.factory_depot = layout.depot_length;
        if (electric_pylon_ && layout.entities.size() > entities.size())
            product.factory_facilities[electric_pylon_->name] +=
                layout.entities.size() - entities.size();
        status_ = BlueprintStatus::Success;
        return true;
    };
    auto signature = plan.area ? LayoutSignature{}
                               : layout_signature(network, graph_, grid_w, grid_h, complex_network,
                                                  standalone_plant);
    if (const auto found = routed_layouts_.find(signature.key);
        !plan.area && found != routed_layouts_.end())
    {
        const auto &cached = found->second;
        log(LogLevel::Info, blueprint_name, ": reusing routed layout from ",
            cached.layout.product_name);
        layout = cached.layout;
        for (size_t i = 0; i < signature.entity_order.size(); ++i)
            layout.entities[signature.entity_order[i]] =
                cached.layout.entities[cached.entity_order[i]];
        for (size_t i = 0; i < signature.stream_order.size(); ++i)
            layout.conveyor_paths[signature.stream_order[i]] =
                cached.layout.conveyor_paths[cached.stream_order[i]];
        layout.product_name = blueprint_name;
        layout.output_per_minute = plan.target_rate * 60;
        layout.startup_items = plan.startup_items;
        layout.startup_stock = plan.startup_stock;
        relabel_layout(layout, network);
        return finish();
    }
    if (reuse_only)
        return false;

    const PlacementProblem problem{blueprint_name,   plan,    module_pylons,
                                   grid_w,           grid_h,  complex_network,
                                   standalone_plant, anchored};
    const auto stages = dependency_rows(network, anchored);
    bool solved = false;
    if (!stages.empty() && options_.placement_seconds > 0)
    {
        auto staged_layout = layout;
        solved = solve_placement(problem, network, stages, staged_layout);
        if (solved)
            layout = std::move(staged_layout);
        else
            log(LogLevel::Info, blueprint_name, ": retrying without dependency rows");
    }
    if (!solved && !solve_placement(problem, network, {}, layout))
        return false;
    if (!plan.area)
        routed_layouts_.emplace(std::move(signature.key),
                                RoutedLayout{layout, std::move(signature.entity_order),
                                             std::move(signature.stream_order)});
    return finish();
}

bool BlueprintSolver::solve_placement(const PlacementProblem &problem, FactoryNetwork network,
                                      const std::map<int, std::vector<int>> &stages,
                                      BlueprintLayout &layout)
{
    const auto &blueprint_name = problem.name;
    const auto &plan = problem.plan;
    const auto &module_pylons = problem.module_pylons;
    const int grid_w = problem.grid_w, grid_h = problem.grid_h;
    const bool complex_network = problem.complex_network,
               standalone_plant = problem.standalone_plant;
    const bool staged = !stages.empty();
    const auto &entities = network.entities;
    auto &streams = network.streams;
    const int root_entity_id = network.root_entity_id, stash_entity_id = network.stash_entity_id;
    const int total_unloaders = network.total_unloaders, unloader_width = network.unloader_width;
    const bool local_bus = plan.area && !plan.area->depot_bus.empty();
    const bool loader = plan.area && plan.area->output_to_depot_bus && !network.delivery;
    auto obstacles = plan.area ? plan.area->depot_bus : std::vector<DepotBusPiece>{};
    if (plan.area && plan.area->rebuilding_port && !network.delivery)
        obstacles.push_back(*plan.area->rebuilding_port);
    std::set<std::pair<int, int>> fixed_tiles;
    for (const auto &piece : obstacles)
        for (int x = piece.x; x < piece.x + piece.width; ++x)
            for (int y = piece.y; y < piece.y + piece.height; ++y)
                fixed_tiles.emplace(x, y);
    const auto attachment_points = local_bus ? depot_attachments(*plan.area, unloader_width)
                                             : std::vector<std::tuple<int, int, int>>{};
    const auto loader_points = loader
                                   ? depot_attachments(*plan.area, entities[stash_entity_id].base_w)
                                   : std::vector<std::tuple<int, int, int>>{};
    if (total_unloaders && plan.area && !local_bus)
        throw std::invalid_argument("No Depot Bus configured in " + plan.area->name);

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

        // Stable port banks leave predictable routing lanes in complex networks.
        // Ground-only routing can vary orientations outside fixed plant modules.
        if (complex_network && !entities[i].is_unloader && !problem.anchored)
        {
            const bool upright =
                entities[i].loop_role == SeedLoopRole::Picker && entities[i].loop_harvest >= 0;
            const int rotation =
                standalone_plant && i == stash_entity_id
                    ? 3
                    : (upright || entities[i].loop_role == SeedLoopRole::Pylon ? 0 : 2);
            if ((!staged || i != stash_entity_id) &&
                (options_.allow_overpasses || standalone_plant || entities[i].loop_harvest >= 0 ||
                 entities[i].loop_role == SeedLoopRole::Pylon))
                model.AddEquality(b[rotation], 1);
            if (!standalone_plant && !staged)
            {
                model.AddGreaterOrEqual(posX[i], 2);
                model.AddGreaterOrEqual(posY[i], 2);
            }
        }

        LinearExpr rot_sum;
        for (const auto &bv : b)
            rot_sum += bv;
        model.AddEquality(rot_sum, 1);
        if (const auto *site = entities[i].site)
        {
            model.AddEquality(posX[i], site->x);
            model.AddEquality(posY[i], site->y);
            model.AddEquality(b[site->rotation / 90], 1);
        }
        if (i == stash_entity_id && network.delivery)
        {
            model.AddEquality(posX[i], network.delivery->x);
            model.AddEquality(posY[i], network.delivery->y);
            model.AddEquality(b[network.delivery->rotation / 90], 1);
        }
        if (const auto *field = entities[i].array_field)
            model.AddEquality(b[field->belt_rotation / 90], 1);
        if (const auto *projector = entities[i].projection)
            model.AddEquality(b[projector->rotation / 90], 1);

        int bw = entities[i].base_w;
        int bh = entities[i].base_h;

        // Unloaders attach to top depot: line Y = grid_h - 1, rotation 0
        if (entities[i].is_unloader && !local_bus)
        {
            model.AddEquality(b[0], 1);
            model.AddEquality(posY[i], grid_h - 1);
            model.AddLessOrEqual(posX[i], grid_w - bw);
        }
        if ((entities[i].is_unloader && local_bus) || (i == stash_entity_id && loader))
        {
            IntVar rotation = model.NewIntVar(Domain(0, 3));
            LinearExpr value;
            for (int r = 0; r < 4; ++r)
                value += LinearExpr::Term(b[r], r);
            model.AddEquality(rotation, value);
            auto allowed = model.AddAllowedAssignments({posX[i], posY[i], rotation});
            for (const auto &[x, y, r] :
                 entities[i].is_unloader ? attachment_points : loader_points)
                allowed.AddTuple({x, y, r});
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
        if (complex_network && !entities[i].is_unloader && !standalone_plant && !staged &&
            !problem.anchored)
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
    if (K > 0 && !local_bus)
    {
        LinearExpr depot_offset;
        if (staged)
            depot_offset = model.NewIntVar(Domain(0, grid_w - 1));
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
            model.AddEquality(posX[u_idx],
                              depot_offset + LinearExpr::Term(unloader_slots[k], unloader_width));
        }
    }

    // Non-overlapping
    NoOverlap2DConstraint no_overlap = model.AddNoOverlap2D();
    for (size_t i = 0; i < intX.size(); ++i)
    {
        no_overlap.AddRectangle(intX[i], intY[i]);
    }
    for (const auto &entity : entities)
    {
        if (entity.climate)
        {
            const int target = entity.id, provider = entity.climate_provider;
            model.AddGreaterOrEqual(posX[target], posX[provider] - entity.climate->range_x);
            model.AddGreaterOrEqual(posY[target], posY[provider] - entity.climate->range_y);
            model.AddLessOrEqual(posX[target] + effW[target],
                                 posX[provider] + effW[provider] + entity.climate->range_x);
            model.AddLessOrEqual(posY[target] + effH[target],
                                 posY[provider] + effH[provider] + entity.climate->range_y);
            for (const auto &other : entities)
                if (other.climate && other.climate->environment != entity.climate->environment)
                {
                    const int source = other.climate_provider;
                    const auto left = model.NewBoolVar(), right = model.NewBoolVar();
                    const auto below = model.NewBoolVar(), above = model.NewBoolVar();
                    model
                        .AddLessOrEqual(posX[target] + effW[target],
                                        posX[source] - other.climate->range_x)
                        .OnlyEnforceIf(left);
                    model
                        .AddGreaterOrEqual(posX[target],
                                           posX[source] + effW[source] + other.climate->range_x)
                        .OnlyEnforceIf(right);
                    model
                        .AddLessOrEqual(posY[target] + effH[target],
                                        posY[source] - other.climate->range_y)
                        .OnlyEnforceIf(below);
                    model
                        .AddGreaterOrEqual(posY[target],
                                           posY[source] + effH[source] + other.climate->range_y)
                        .OnlyEnforceIf(above);
                    model.AddBoolOr({left, right, below, above});
                }
        }
        if (const auto *projector = entity.projection)
        {
            model.AddEquality(posX[entity.id], posX[entity.projection_target] + projector->dx);
            model.AddEquality(posY[entity.id], posY[entity.projection_target] + projector->dy);
        }
    }
    for (const auto &piece : obstacles)
        no_overlap.AddRectangle(model.NewFixedSizeIntervalVar(LinearExpr(piece.x), piece.width),
                                model.NewFixedSizeIntervalVar(LinearExpr(piece.y), piece.height));

    if (complex_network && !staged && !problem.anchored)
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
                model.AddEquality(rotB[entity.id][0], 1);
                model.AddEquality(rotB[partner][2], 1);
                model.AddEquality(rotB[harvest][2], 1);
                model.AddEquality(rotB[pole][0], 1);
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
                    model.AddEquality(rotB[stash_entity_id][3], 1);
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

    if (!problem.anchored && !staged && (!complex_network || options_.allow_overpasses) &&
        !primary_consumer_ids.empty() && primary_total_w <= grid_w)
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

    if (staged)
    {
        std::vector<int> projected_x(N, -1);
        const auto *previous = &stages.at(0);
        if (local_bus)
            for (const int id : *previous)
            {
                model.AddEquality(posY[id], posY[previous->front()]);
                model.AddEquality(rotB[id][0], 1);
            }
        for (const auto &[stage, row] : stages)
        {
            if (stage == 0)
                continue;
            // Wide rows need room for lateral movement between their port banks.
            const int gap = previous->size() >= 8 || row.size() >= 8 ? 4 : problem.anchored ? 2 : 1;
            for (const int id : row)
            {
                model.AddEquality(posY[id], posY[row.front()]);
                model.AddLessOrEqual(posY[id] + effH[id] + gap, posY[previous->front()]);
                model.AddEquality(rotB[id][2], 1);
            }
            previous = &row;
        }

        // Order each row by the X coordinates of its downstream port banks.
        for (auto stage = stages.rbegin(); stage != stages.rend(); ++stage)
        {
            std::vector<std::pair<int, int>> ordered;
            int width = 0;
            for (const int id : stage->second)
            {
                int sum = 0, links = 0;
                for (const auto &stream : streams)
                    if (stream.src_entity == id && projected_x[stream.dst_entity] >= 0)
                    {
                        const auto &dst = entities[stream.dst_entity];
                        const auto &src = entities[id];
                        const int input_x = dst.base_w - 1 - dst.inputs[stream.dst_port_idx].x;
                        const int output_x =
                            src.is_unloader ? src.outputs[stream.src_port_idx].x
                                            : src.base_w - 1 - src.outputs[stream.src_port_idx].x;
                        sum += projected_x[dst.id] + input_x - output_x;
                        ++links;
                    }
                ordered.emplace_back(links ? sum / links : (grid_w - entities[id].base_w) / 2, id);
                width += entities[id].base_w;
            }
            if (width > grid_w)
                continue;
            std::sort(ordered.begin(), ordered.end());
            int x = std::clamp(ordered.front().first, 0, grid_w - width);
            int left = -1;
            for (const auto &entry : ordered)
            {
                const int id = entry.second;
                projected_x[id] = x;
                x += entities[id].base_w;
                if (left >= 0)
                    model.AddLessOrEqual(posX[left] + effW[left], posX[id]);
                left = id;
            }
        }
    }

    // Root packaging unit: inputs face North toward intermediate converters, outputs face South
    if (entities[root_entity_id].type == "packaging_unit")
    {
        model.AddEquality(rotB[root_entity_id][2], 1); // rot 180: inputs North, outputs South
    }

    // Protocol stash is at or below root facility
    if (!problem.anchored)
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
    std::vector<std::pair<IntVar, IntVar>> reserved_approaches;

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
                for (const auto &piece : obstacles)
                {
                    const auto left = model.NewBoolVar(), right = model.NewBoolVar();
                    const auto below = model.NewBoolVar(), above = model.NewBoolVar();
                    model.AddLessThan(ax, piece.x).OnlyEnforceIf(left);
                    model.AddGreaterOrEqual(ax, piece.x + piece.width).OnlyEnforceIf(right);
                    model.AddLessThan(ay, piece.y).OnlyEnforceIf(below);
                    model.AddGreaterOrEqual(ay, piece.y + piece.height).OnlyEnforceIf(above);
                    model.AddBoolOr({left, right, below, above});
                }

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
                    reserved_approaches.emplace_back(ax2, ay2);

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
    if (!complex_network && !problem.anchored && max_dim < grid_w)
    {
        model.AddLessOrEqual(bbox_w, max_dim);
        model.AddLessOrEqual(bbox_h, max_dim);
    }
    if (problem.anchored && options_.blueprint_size)
    {
        model.AddLessOrEqual(bbox_w, options_.blueprint_size->first);
        model.AddLessOrEqual(bbox_h, options_.blueprint_size->second);
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
    model.Minimize(bbox_w * 100 + bbox_h * 100 + stream_dist * (staged ? 25 : 5));

    if (complex_network && !standalone_plant && !staged && !problem.anchored)
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
    const double placement_limit =
        options_.placement_seconds <= 0
            ? 0
            : (problem.anchored
                   ? std::max(options_.placement_seconds, std::clamp(N * 0.5, 10.0, 30.0))
                   : (complex_network ? std::max(options_.placement_seconds, 6.0)
                                      : options_.placement_seconds));
    params.set_max_time_in_seconds(placement_limit);
    params.set_stop_after_first_solution(complex_network && !staged && !problem.anchored);
    params.set_num_search_workers(8);
    params.set_random_seed(42);

    const int max_conveyor_attempts = options_.max_attempts;
    int attempted_placements = 0;
    bool power_coverage_failed = false;
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::time_point::max();
    auto set_deadline = [&](double seconds)
    {
        const auto now = Clock::now();
        const std::chrono::duration<double> budget(seconds);
        deadline = budget < Clock::time_point::max() - now
                       ? now + std::chrono::duration_cast<Clock::duration>(budget)
                       : Clock::time_point::max();
    };
    if (staged || problem.anchored)
        set_deadline(placement_limit);
    std::optional<BlueprintLayout> best_layout;
    std::pair<int, int> initial_dimensions;
    IntVar placement_area;
    const int64_t area_weight = int64_t{grid_w} + grid_h + 1;
    auto footprint_score = [&](const BlueprintLayout &candidate)
    {
        const int64_t w = candidate.bounding_width, h = candidate.bounding_height;
        return std::make_tuple(w * h, candidate.belt_tiles(), w + h);
    };

    for (int attempt = 1; best_layout || attempt <= max_conveyor_attempts; ++attempt)
    {
        params.set_random_seed(42 + attempt * 17);
        auto placement_model = model.Build();
        if (staged || problem.anchored || best_layout)
        {
            const double remaining = std::chrono::duration<double>(deadline - Clock::now()).count();
            if (remaining <= 0)
                break;
            // Leave time in the initial staged budget for routing and another placement.
            const double candidate_time =
                problem.anchored ? std::min(5.0, placement_limit / 8) : placement_limit / 2;
            params.set_max_time_in_seconds(best_layout ? remaining
                                                       : std::min(remaining, candidate_time));
        }
        Model cp_model;
        cp_model.Add(NewSatParameters(params));
        ++attempted_placements;
        CpSolverResponse response = SolveCpModel(placement_model, &cp_model);

        if (response.status() != CpSolverStatus::OPTIMAL &&
            response.status() != CpSolverStatus::FEASIBLE)
        {
            if (response.status() == CpSolverStatus::MODEL_INVALID)
                throw std::runtime_error("Invalid blueprint model: " + response.solution_info());
            if (best_layout)
                break;
            if (problem.anchored && response.status() == CpSolverStatus::UNKNOWN &&
                Clock::now() < deadline)
                continue;
            status_ = response.status() == CpSolverStatus::INFEASIBLE ? BlueprintStatus::Infeasible
                                                                      : BlueprintStatus::Timeout;
            log(staged ? LogLevel::Debug : LogLevel::Warning, blueprint_name, ": ",
                status_ == BlueprintStatus::Timeout ? "placement search timed out"
                                                    : "placement model infeasible");
            return false;
        }

        layout.entities.clear();
        std::map<std::string, int> type_instance_count;
        for (int i = 0; i < N; ++i)
        {
            PlacedBlueprintEntity pe;
            set_entity_labels(pe, entities[i]);
            pe.type = entities[i].type;
            pe.x = SolutionIntegerValue(response, posX[i]);
            pe.y = SolutionIntegerValue(response, posY[i]);
            pe.width = SolutionIntegerValue(response, effW[i]);
            pe.height = SolutionIntegerValue(response, effH[i]);
            pe.is_unloader = entities[i].is_unloader;

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
        layout.blocked_tiles.assign(fixed_tiles.begin(), fixed_tiles.end());
        const auto routing =
            route_conveyors(layout, streams, grid_w, grid_h, deadline, options_.allow_overpasses,
                            plan.area && plan.area->allow_pipe_over_belt);
        const auto &conflicted_stream_indices = routing.conflicted_stream_indices;
        bool complete = routing.feasible;
        std::vector<std::pair<int, int>> reserved_cells;
        if (complete)
        {
            for (const auto &[x, y] : reserved_approaches)
                reserved_cells.emplace_back(SolutionIntegerValue(response, x),
                                            SolutionIntegerValue(response, y));
            reserved_cells.insert(reserved_cells.end(), fixed_tiles.begin(), fixed_tiles.end());
            if (electric_pylon_)
                complete = place_electric_pylons(layout, network, *electric_pylon_, grid_w, grid_h,
                                                 reserved_cells, deadline);
        }
        if (Clock::now() >= deadline)
            break;
        if (complete)
        {
            if (!best_layout && attempt > 1)
            {
                log(LogLevel::Debug, "Routing feasible for ", blueprint_name, " on attempt ",
                    attempt);
            }

            layout.product_name = blueprint_name;
            layout.depot_length =
                total_unloaders * unloader_width + (loader ? entities[stash_entity_id].base_w : 0);
            layout.needed_climates.clear();
            layout.emitted_climates.clear();
            for (const auto &entity : entities)
                if (const auto *climate = entity.climate)
                {
                    const auto &target = layout.entities[entity.id];
                    const auto &provider = layout.entities[entity.climate_provider];
                    layout.needed_climates.push_back(
                        {climate->environment, target.x, target.y, target.width, target.height});
                    layout.emitted_climates.push_back(
                        {climate->environment, provider.x - climate->range_x,
                         provider.y - climate->range_y, provider.width + 2 * climate->range_x,
                         provider.height + 2 * climate->range_y});
                }
            const auto normalization_offset = layout.normalize();
            if (problem.anchored && options_.blueprint_size &&
                (layout.bounding_width > options_.blueprint_size->first ||
                 layout.bounding_height > options_.blueprint_size->second))
                continue;
            layout.blocked_tiles.clear();
            if (problem.anchored)
            {
                layout.fixed_area = plan.area->name;
                layout.area_origin = normalization_offset;
            }

            if (!best_layout)
            {
                set_deadline(options_.compact_seconds);
                initial_dimensions = {layout.bounding_width, layout.bounding_height};
            }
            if (!standalone_plant && !plan.area)
            {
                const auto [x, y] = normalization_offset;
                for (auto &cell : reserved_cells)
                {
                    cell.first -= x;
                    cell.second -= y;
                }
                const auto repair_deadline =
                    best_layout ? Clock::time_point::min()
                                : std::min(deadline, Clock::now() + std::chrono::seconds(1));
                compact_routed_blueprint(layout, streams, std::move(reserved_cells),
                                         repair_deadline);
            }
            if (!best_layout)
            {
                best_layout = layout;
                if (options_.compact_seconds == 0 || standalone_plant || problem.anchored)
                    break;
                params.set_stop_after_first_solution(true);
                model.ClearHints();
                model.AddLessOrEqual(bbox_w, initial_dimensions.first);
                model.AddLessOrEqual(bbox_h, initial_dimensions.second);
                for (const auto &port : port_accesses)
                {
                    model.AddLessOrEqual(min_x, port.ax);
                    model.AddLessOrEqual(min_y, port.ay);
                    model.AddGreaterThan(max_x, port.ax);
                    model.AddGreaterThan(max_y, port.ay);
                }
                // The machines and their belt endpoints give a lower bound on the routed area.
                placement_area = model.NewIntVar(Domain(total_fac_area, int64_t{grid_w} * grid_h));
                model.AddMultiplicationEquality(placement_area, {bbox_w, bbox_h});
                model.Minimize(placement_area * area_weight + bbox_w + bbox_h);
                model.AddLessOrEqual(placement_area, std::get<0>(footprint_score(*best_layout)));
            }
            else if (layout.bounding_width <= initial_dimensions.first &&
                     layout.bounding_height <= initial_dimensions.second &&
                     footprint_score(layout) < footprint_score(*best_layout))
            {
                best_layout = layout;
                model.AddLessOrEqual(placement_area, std::get<0>(footprint_score(*best_layout)));
            }
        }
        else if (routing.feasible)
        {
            power_coverage_failed = true;
            log(LogLevel::Debug, blueprint_name,
                ": could not place pylons for routed layout on attempt ", attempt);
        }
        else
            log(LogLevel::Debug, "Routing conflict for ", blueprint_name, " on attempt ", attempt,
                ": ", conflicted_stream_indices.size(),
                " conflicted streams; adding placement cuts");

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
        if (complete || problem.anchored)
            continue;

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

    if (best_layout)
    {
        layout = std::move(*best_layout);
        if (initial_dimensions.first > 0 &&
            initial_dimensions != std::make_pair(layout.bounding_width, layout.bounding_height))
            log(LogLevel::Info, blueprint_name, ": compacted blueprint from ",
                initial_dimensions.first, " x ", initial_dimensions.second, " to ",
                layout.bounding_width, " x ", layout.bounding_height);
        return true;
    }

    status_ = BlueprintStatus::RoutingExhausted;
    log(staged ? LogLevel::Debug : LogLevel::Warning, blueprint_name,
        power_coverage_failed ? ": routing and power coverage search exhausted after "
                              : ": routing search exhausted after ",
        attempted_placements, " placements");
    return false;
}

} // namespace aic
