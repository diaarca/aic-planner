#include "production_network.hpp"
#include "logging.hpp"
#include "ortools/sat/cp_model.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace aic
{
namespace
{
std::vector<Port> solid_ports(const std::vector<Port> &ports)
{
    std::vector<Port> result;
    for (const auto &port : ports)
        if (port.type == TransportMedium::Solid)
            result.push_back(port);
    return result;
}
int64_t denominator(double rate)
{
    for (int64_t value = 1; value <= 10000; ++value)
        if (std::abs(rate * value - std::round(rate * value)) < 1e-7)
            return value;
    throw std::invalid_argument("Flow precision exceeds supported rational scale");
}
} // namespace
FactoryNetwork build_factory_network(const ProductionPlan &plan, const std::string &target,
                                     const Facility &stash, const Facility &unloader)
{
    using namespace operations_research;
    using namespace operations_research::sat;
    FactoryNetwork network;
    auto &entities = network.entities;
    if (unloader.height != 1 || unloader.outputs.size() != 1 || unloader.power != 0)
        throw std::invalid_argument("Unsupported depot unloader geometry or power");
    network.unloader_width = unloader.width;
    std::map<std::string, int> counts;
    for (const auto &step : plan.steps)
        for (int instance = 0; instance < step.instances; ++instance)
        {
            FactoryEntity entity;
            entity.id = entities.size();
            entity.type = step.facility->name;
            entity.name = entity.type + "_" + std::to_string(++counts[entity.type]);
            entity.base_w = step.facility->width;
            entity.base_h = step.facility->height;
            entity.inputs = solid_ports(step.facility->inputs);
            entity.outputs = solid_ports(step.facility->outputs);
            entity.recipe = step.recipe;
            entity.loop_role = step.loop_role;
            if (!step.seed_loop.empty())
                entity.seed_loop = step.seed_loop + ":" + std::to_string(instance + 1);
            const double rate = step.cycles_per_second / step.instances;
            for (const auto &ingredient : step.recipe->ingredients)
                entity.input_rates[ingredient.item] += rate * ingredient.amount;
            const auto &product = step.recipe->products.front();
            entity.item_produced = product.item;
            entity.output_rates[product.item] = rate * product.amount;
            if (entity.input_rates.size() == 1)
                entity.item_consumed = entity.input_rates.begin()->first;
            if (product.item == target && network.root_entity_id < 0)
                network.root_entity_id = entity.id;
            entities.push_back(entity);
        }
    for (auto &entity : entities)
        if (entity.loop_role != SeedLoopRole::None)
            for (const auto &partner : entities)
                if (entity.seed_loop == partner.seed_loop)
                {
                    if ((entity.loop_role == SeedLoopRole::Picker &&
                         partner.loop_role == SeedLoopRole::Planter) ||
                        (entity.loop_role != SeedLoopRole::Picker &&
                         partner.loop_role == SeedLoopRole::Picker))
                        entity.loop_partner = partner.id;
                    if (partner.loop_role == SeedLoopRole::Harvest)
                        entity.loop_harvest = partner.id;
                }
    for (const auto &entity : entities)
        if (entity.loop_role != SeedLoopRole::None && entity.loop_partner < 0)
            throw std::invalid_argument("Unpaired seed-loop machine");
    if (entities.size() > 200)
        throw std::invalid_argument("Blueprint exceeds 200 manufacturing entities");
    FactoryEntity sink;
    sink.id = entities.size();
    sink.name = sink.type = "protocol_stash";
    sink.base_w = stash.width;
    sink.base_h = stash.height;
    sink.inputs = solid_ports(stash.inputs);
    sink.input_rates[target] = plan.target_rate;
    network.stash_entity_id = sink.id;
    entities.push_back(sink);
    const size_t manufacturing_count = entities.size() - 1;
    for (size_t destination = 0; destination < manufacturing_count; ++destination)
    {
        const auto demands = entities[destination].input_rates;
        for (const auto &[item, rate] : demands)
        {
            if (!plan.external_rates.count(item))
                continue;
            const int number = static_cast<int>(std::ceil(rate / 0.5 - 1e-8));
            for (int index = 0; index < number; ++index)
            {
                FactoryEntity source;
                source.id = entities.size();
                source.type = "unloader";
                source.name = "unloader_" + item + "_" + std::to_string(++network.total_unloaders);
                source.base_w = unloader.width;
                source.base_h = unloader.height;
                source.is_unloader = true;
                source.item_produced = item;
                source.output_rates[item] = rate / number;
                source.dedicated_destination = destination;
                Port outlet = unloader.outputs.front();
                if (outlet.fx == 0 && outlet.fy == 1)
                {
                    source.base_rotation = 180;
                    outlet.x = unloader.width - 1 - outlet.x;
                    outlet.y = unloader.height - 1 - outlet.y;
                    outlet.fx = -outlet.fx;
                    outlet.fy = -outlet.fy;
                }
                if (outlet.fx != 0 || outlet.fy != -1 || outlet.type != TransportMedium::Solid)
                    throw std::invalid_argument("Unsupported depot outlet orientation");
                source.outputs.push_back(outlet);
                entities.push_back(source);
            }
        }
    }
    // Recover the small rational rates from the LP so flow equalities are exact in CP-SAT.
    int64_t scale = 2;
    for (const auto &entity : entities)
        for (const auto *rates : {&entity.input_rates, &entity.output_rates})
            for (const auto &[item, rate] : *rates)
            {
                const auto next = denominator(rate);
                const auto factor = next / std::gcd(scale, next);
                if (factor > 10000000 / scale)
                    throw std::invalid_argument("Flow denominator exceeds supported precision");
                scale *= factor;
            }
    CpModelBuilder model;
    struct Edge
    {
        int source, destination;
        std::string item;
        IntVar amount;
        BoolVar active;
    };
    std::vector<Edge> edges;
    std::vector<std::vector<BoolVar>> inputs(entities.size()), outputs(entities.size());
    std::map<std::pair<int, std::string>, std::vector<IntVar>> incoming, outgoing;
    LinearExpr objective;
    for (const auto &source : entities)
        for (const auto &[item, rate] : source.output_rates)
            for (const auto &destination : entities)
            {
                if (!destination.input_rates.count(item))
                    continue;
                if (source.dedicated_destination >= 0 &&
                    source.dedicated_destination != destination.id)
                    continue;
                // Keep every feedback module closed: the picker receives flowers
                // only from its planter, and that planter receives its return seed
                // only from its picker. The spare seed may feed any external consumer.
                if (source.loop_role == SeedLoopRole::Planter &&
                    source.loop_partner != destination.id)
                    continue;
                if (destination.loop_role != SeedLoopRole::None &&
                    destination.loop_partner != source.id)
                    continue;
                if (source.loop_role == SeedLoopRole::Picker && source.loop_harvest >= 0 &&
                    destination.id != source.loop_partner && destination.id != source.loop_harvest)
                    continue;
                const int parallel = static_cast<int>(
                    std::ceil(std::min(rate, destination.input_rates.at(item)) / 0.5 - 1e-8));
                for (int lane = 0; lane < parallel; ++lane)
                {
                    auto amount = model.NewIntVar(Domain(0, scale / 2));
                    auto active = model.NewBoolVar();
                    model.AddLessOrEqual(amount, LinearExpr::Term(active, scale / 2));
                    model.AddGreaterOrEqual(amount, active);
                    edges.push_back({source.id, destination.id, item, amount, active});
                    incoming[{destination.id, item}].push_back(amount);
                    outgoing[{source.id, item}].push_back(amount);
                    inputs[destination.id].push_back(active);
                    outputs[source.id].push_back(active);
                    objective +=
                        LinearExpr::Term(active, 100000 + std::abs(source.id - destination.id));
                    if (lane > 0)
                        model.AddGreaterOrEqual(edges[edges.size() - 2].amount, amount);
                }
            }
    for (const auto &entity : entities)
    {
        for (const auto &[item, rate] : entity.input_rates)
            model.AddEquality(LinearExpr::Sum(incoming[{entity.id, item}]),
                              std::llround(rate * scale));
        for (const auto &[item, rate] : entity.output_rates)
            model.AddEquality(LinearExpr::Sum(outgoing[{entity.id, item}]),
                              std::llround(rate * scale));
        model.AddLessOrEqual(LinearExpr::Sum(inputs[entity.id]), entity.inputs.size());
        model.AddLessOrEqual(LinearExpr::Sum(outputs[entity.id]), entity.outputs.size());
    }
    model.Minimize(objective);
    SatParameters parameters;
    parameters.set_num_search_workers(1);
    parameters.set_max_time_in_seconds(5);
    Model context;
    context.Add(NewSatParameters(parameters));
    const auto response = SolveCpModel(model.Build(), &context);
    if (response.status() != CpSolverStatus::OPTIMAL &&
        response.status() != CpSolverStatus::FEASIBLE)
    {
        log(LogLevel::Debug, "Flow allocation status: ", CpSolverStatus_Name(response.status()),
            " ", response.solution_info());
        for (const auto &entity : entities)
        {
            std::string rates;
            for (const auto &[item, rate] : entity.input_rates)
                rates += " in " + item + "=" + std::to_string(rate);
            for (const auto &[item, rate] : entity.output_rates)
                rates += " out " + item + "=" + std::to_string(rate);
            log(LogLevel::Debug, entity.name, " ports ", entity.inputs.size(), "/",
                entity.outputs.size(), rates);
        }
        throw std::invalid_argument(
            "Cannot allocate balanced conveyor flows to available machine ports");
    }
    std::vector<std::set<int>> used_inputs(entities.size()), used_outputs(entities.size());
    auto port_index = [](size_t count, std::set<int> &used, int fixed) -> int
    {
        if (fixed >= 0)
        {
            if (fixed >= static_cast<int>(count) || !used.insert(fixed).second)
                throw std::invalid_argument("Plant template port collision");
            return fixed;
        }
        if (count == 3 && !used.count(1))
        {
            used.insert(1);
            return 1;
        }
        for (size_t index = 0; index < count; ++index)
            if (used.insert(index).second)
                return index;
        throw std::invalid_argument("No unused conveyor port");
    };
    for (const auto &edge : edges)
        if (SolutionBooleanValue(response, edge.active))
        {
            const auto &source = entities[edge.source];
            const auto &destination = entities[edge.destination];
            int source_fixed = -1, destination_fixed = -1;
            if (source.loop_role == SeedLoopRole::Picker && source.loop_harvest >= 0)
            {
                source_fixed = destination.loop_role == SeedLoopRole::Planter ? 0 : 4;
                destination_fixed = source_fixed;
            }
            if (source.loop_role == SeedLoopRole::Planter && source.loop_harvest >= 0)
                source_fixed = destination_fixed = 0;
            if (source.loop_role == SeedLoopRole::Harvest && !used_outputs[edge.source].count(4))
                source_fixed = 4;
            const int source_port =
                port_index(source.outputs.size(), used_outputs[edge.source], source_fixed);
            const int destination_port = port_index(
                destination.inputs.size(), used_inputs[edge.destination], destination_fixed);
            network.streams.push_back(
                {edge.source, source_port, edge.destination, destination_port, edge.item,
                 static_cast<double>(SolutionIntegerValue(response, edge.amount)) / scale});
        }
    return network;
}
} // namespace aic
