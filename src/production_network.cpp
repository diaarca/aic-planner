#include "production_network.hpp"
#include "logging.hpp"
#include "ortools/sat/cp_model.h"
#include "production_network_internal.hpp"
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
                                     const Facility &stash, const Facility &unloader,
                                     const DepotBusPiece *delivery)
{
    using namespace operations_research;
    using namespace operations_research::sat;
    FactoryNetwork network;
    network.delivery = delivery;
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
            entity.needs_power = step.facility->needs_power;
            entity.inputs = plan.area ? step.facility->inputs : solid_ports(step.facility->inputs);
            entity.outputs =
                plan.area ? step.facility->outputs : solid_ports(step.facility->outputs);
            if (step.loop_role != SeedLoopRole::None)
            {
                entity.inputs = solid_ports(entity.inputs);
                entity.outputs = solid_ports(entity.outputs);
            }
            entity.recipe = step.recipe;
            entity.cycle_seconds = step.recipe->time;
            if (plan.area && !array_mode(*step.recipe).empty())
            {
                entity.array_field = &plan.area->array_fields.at(array_mode(*step.recipe));
                entity.cycle_seconds =
                    std::max(entity.cycle_seconds, entity.array_field->cycle_seconds);
                entity.type = "array_belt";
                entity.name = entity.type + "_" + std::to_string(++counts[entity.type]);
                entity.base_w = entity.base_h = 1;
                entity.needs_power = false;
                entity.inputs = {{0, 0, TransportMedium::Solid, -1, 0, 0}};
                entity.outputs = {{0, 0, TransportMedium::Solid, 1, 0, 0}};
            }
            entity.loop_role = step.loop_role;
            if (!step.seed_loop.empty())
                entity.seed_loop = step.seed_loop + ":" + std::to_string(instance + 1);
            const double rate = step.cycles_per_second / step.instances;
            for (const auto &ingredient : step.recipe->ingredients)
            {
                entity.input_rates[ingredient.item] += rate * ingredient.amount;
                if (plan.area)
                    bind_ports(ingredient, entity.inputs, entity.input_ports);
            }
            if (plan.area && step.facility->activation)
            {
                entity.activation = &*step.facility->activation;
                const auto &feed = *entity.activation;
                const auto position =
                    std::find_if(entity.inputs.begin(), entity.inputs.end(),
                                 [&](const Port &port) { return port.source_index == feed.input; });
                if (position == entity.inputs.end() || position->type != TransportMedium::Pipe)
                    throw std::invalid_argument("Missing activation port on " + entity.type);
                entity.activation_port = std::distance(entity.inputs.begin(), position);
                for (auto &[item, ports] : entity.input_ports)
                    ports.erase(std::remove(ports.begin(), ports.end(), entity.activation_port),
                                ports.end());
                entity.input_ports[feed.item].push_back(entity.activation_port);
                entity.input_rates[feed.item] += feed.rate;
            }
            if (plan.area && step.recipe->environment != 0)
                entity.climate = &plan.area->climates.at(step.recipe->environment);
            entity.item_produced = step.recipe->products.front().item;
            for (const auto &product : step.recipe->products)
            {
                entity.output_rates[product.item] += rate * product.amount;
                if (plan.area)
                    bind_ports(product, entity.outputs, entity.output_ports);
            }
            if (entity.input_rates.size() == 1)
                entity.item_consumed = entity.input_rates.begin()->first;
            if (entity.output_rates.count(target) && network.root_entity_id < 0)
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
    const auto phase = [&](const std::string &item) { return planned_phase(plan, item); };
    const size_t production_count = entities.size();
    for (size_t i = 0; i < production_count; ++i)
    {
        if (const auto *climate = entities[i].climate)
        {
            entities[i].climate_provider = entities.size();
            add_factory_consumer(network, counts, *plan.disperser, climate->gas,
                                 climate->limit / 60, MaterialPhase::Gas);
            entities.back().climate_feed = true;
        }
        if (const auto *field = entities[i].array_field)
            for (const auto &projector : field->projectors)
            {
                add_factory_consumer(network, counts, *plan.projector, "item_gas_xiranite",
                                     projector.feed / 60, MaterialPhase::Gas);
                entities.back().projection = &projector;
                entities.back().projection_target = i;
            }
    }
    for (const auto &[item, output] : plan.disposal)
    {
        const auto &[facility, rate] = output;
        const int count = std::ceil(rate / facility->disposal_rates.at(item) - 1e-8);
        for (int i = 0; i < count; ++i)
            add_factory_consumer(network, counts, *facility, item, rate / count, phase(item));
    }
    FactoryEntity sink;
    sink.id = entities.size();
    sink.name = sink.type = stash.name;
    sink.base_w = stash.width;
    sink.base_h = stash.height;
    sink.needs_power = stash.needs_power;
    sink.inputs = solid_ports(stash.inputs);
    sink.input_rates[target] = plan.target_rate;
    if (phase(target) != MaterialPhase::Solid)
        throw std::invalid_argument("A pipe output requires an explicit permanent destination");
    if (plan.area)
        bind_ports(RecipeIngredient{target, plan.target_rate, phase(target), {}}, sink.inputs,
                   sink.input_ports);
    network.stash_entity_id = sink.id;
    entities.push_back(sink);
    const size_t manufacturing_count = entities.size() - 1;
    auto external_remaining = plan.external_rates;
    for (size_t destination = 0; destination < manufacturing_count; ++destination)
    {
        const auto demands = entities[destination].input_rates;
        for (const auto &[item, rate] : demands)
        {
            if (phase(item) != MaterialPhase::Solid)
                continue;
            if (!external_remaining.count(item) || external_remaining.at(item) <= 1e-9)
                continue;
            const double external = std::min(rate, external_remaining.at(item));
            external_remaining[item] -= external;
            const int number =
                static_cast<int>(std::ceil(external * solid_belt_seconds_per_item - 1e-8));
            for (int index = 0; index < number; ++index)
            {
                FactoryEntity source;
                source.id = entities.size();
                source.type = "unloader";
                source.name = "unloader_" + item + "_" + std::to_string(++network.total_unloaders);
                source.base_w = unloader.width;
                source.base_h = unloader.height;
                source.is_unloader = true;
                source.needs_power = unloader.needs_power;
                source.item_produced = item;
                source.output_rates[item] = external / number;
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
                if (plan.area)
                    bind_ports(RecipeProduct{item, external / number, phase(item), {}},
                               source.outputs, source.output_ports);
                entities.push_back(source);
            }
        }
    }
    add_pipe_distribution(plan, network, counts);
    if (entities.size() > 250)
        throw std::invalid_argument("Blueprint exceeds 250 facilities including logistics");
    // Recover the small rational rates from the LP so flow equalities are exact in CP-SAT.
    int64_t scale = solid_belt_seconds_per_item;
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
        std::vector<std::pair<int, BoolVar>> source_ports, destination_ports;
    };
    std::vector<Edge> edges;
    std::vector<std::vector<BoolVar>> inputs(entities.size()), outputs(entities.size());
    std::map<std::pair<int, std::string>, std::vector<IntVar>> incoming, outgoing;
    std::map<std::pair<int, int>, std::vector<BoolVar>> physical_inputs, physical_outputs;
    std::map<int, std::vector<IntVar>> activation_flows;
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
                const auto pipe = source.pipe_destinations.find(item);
                if (pipe != source.pipe_destinations.end() && !pipe->second.count(destination.id))
                    continue;
                if (source.id == destination.id && phase(item) == MaterialPhase::Solid)
                    continue;
                // Keep every feedback module closed: the picker receives flowers
                // only from its planter, and that planter receives its return seed
                // only from its picker. The spare seed may feed any external consumer.
                if (source.loop_role == SeedLoopRole::Planter &&
                    source.loop_partner != destination.id)
                    continue;
                if (destination.loop_role != SeedLoopRole::None &&
                    phase(item) == MaterialPhase::Solid && destination.loop_partner != source.id)
                    continue;
                if (source.loop_role == SeedLoopRole::Picker && source.loop_harvest >= 0 &&
                    destination.id != source.loop_partner && destination.id != source.loop_harvest)
                    continue;
                int source_fixed = -1, destination_fixed = -1;
                if (source.loop_role == SeedLoopRole::Picker && source.loop_harvest >= 0)
                    source_fixed = destination_fixed =
                        destination.loop_role == SeedLoopRole::Planter ? 0 : 4;
                if (source.loop_role == SeedLoopRole::Planter && source.loop_harvest >= 0)
                    source_fixed = destination_fixed = 0;
                if (source.loop_role == SeedLoopRole::Harvest &&
                    source.output_rates.at(item) <= 0.5)
                    source_fixed = 4;
                const double capacity = transport_capacity(phase(item));
                const auto lane_capacity = std::llround(capacity * scale);
                int parallel = static_cast<int>(
                    std::ceil(std::min(rate, destination.input_rates.at(item)) / capacity - 1e-8));
                if (destination.activation && destination.activation->item == item &&
                    destination.input_rates.at(item) > destination.activation->rate + 1e-9)
                    parallel = std::max(parallel, 2);
                for (int lane = 0; lane < parallel; ++lane)
                {
                    auto amount = model.NewIntVar(Domain(0, lane_capacity));
                    auto active = model.NewBoolVar();
                    model.AddLessOrEqual(amount, LinearExpr::Term(active, lane_capacity));
                    model.AddGreaterOrEqual(amount, active);
                    edges.push_back({source.id, destination.id, item, amount, active, {}, {}});
                    if (plan.area)
                    {
                        const auto assign = [&](int entity, const std::vector<int> &eligible,
                                                auto &choices, auto &reservations, bool input,
                                                int fixed)
                        {
                            LinearExpr chosen;
                            for (const auto index : eligible)
                            {
                                if (fixed >= 0 && index != fixed)
                                    continue;
                                const auto used = model.NewBoolVar();
                                choices.emplace_back(index, used);
                                reservations[{entity, index}].push_back(used);
                                chosen += used;
                                if (input && index == destination.activation_port)
                                {
                                    auto feed = model.NewIntVar(Domain(0, lane_capacity));
                                    model.AddEquality(feed, amount).OnlyEnforceIf(used);
                                    model.AddEquality(feed, 0).OnlyEnforceIf(used.Not());
                                    activation_flows[entity].push_back(feed);
                                }
                            }
                            model.AddEquality(chosen, active);
                        };
                        assign(source.id, source.output_ports.at(item), edges.back().source_ports,
                               physical_outputs, false, source_fixed);
                        assign(destination.id, destination.input_ports.at(item),
                               edges.back().destination_ports, physical_inputs, true,
                               destination_fixed);
                    }
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
        if (entity.activation)
            model.AddEquality(LinearExpr::Sum(activation_flows[entity.id]),
                              std::llround(entity.activation->rate * scale));
    }
    for (const auto *ports : {&physical_inputs, &physical_outputs})
        for (const auto &[port, usages] : *ports)
            model.AddAtMostOne(usages);
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
        throw std::invalid_argument("Cannot allocate balanced material flows to machine ports");
    }
    std::vector<std::set<int>> used_inputs(entities.size()), used_outputs(entities.size());
    const auto interchangeable = [&](const FactoryEntity &entity, bool input)
    {
        if (entity.loop_role != SeedLoopRole::None)
            return false;
        const auto &bindings = input ? entity.input_ports : entity.output_ports;
        const std::vector<int> *common = nullptr;
        for (const auto &[item, allowed] : bindings)
        {
            if (phase(item) != MaterialPhase::Solid)
                continue;
            if (common && *common != allowed)
                return false;
            common = &allowed;
        }
        return true;
    };
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
            const auto chosen_port = [&](const auto &choices)
            {
                for (const auto &[index, used] : choices)
                    if (SolutionBooleanValue(response, used))
                        return index;
                throw std::logic_error("Flow allocation omitted a physical port");
            };
            const int source_port = plan.area ? chosen_port(edge.source_ports)
                                              : port_index(source.outputs.size(),
                                                           used_outputs[edge.source], source_fixed);
            const int destination_port =
                plan.area ? chosen_port(edge.destination_ports)
                          : port_index(destination.inputs.size(), used_inputs[edge.destination],
                                       destination_fixed);
            const auto medium = phase(edge.item);
            const bool fixed =
                plan.area && (medium != MaterialPhase::Solid || !interchangeable(source, false) ||
                              !interchangeable(destination, true));
            network.streams.push_back(
                {edge.source,
                 source_port,
                 edge.destination,
                 destination_port,
                 edge.item,
                 static_cast<double>(SolutionIntegerValue(response, edge.amount)) / scale,
                 {},
                 medium,
                 fixed});
        }
    return network;
}
} // namespace aic
