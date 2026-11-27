#pragma once

#include "production_network.hpp"
#include <algorithm>

namespace aic
{
inline MaterialPhase planned_phase(const ProductionPlan &plan, const std::string &item)
{
    const auto found = plan.phases.find(item);
    return found == plan.phases.end() ? MaterialPhase::Solid : found->second;
}

template <class Material>
void bind_ports(const Material &material, const std::vector<Port> &ports,
                std::map<std::string, std::vector<int>> &assignments)
{
    const auto medium =
        material.phase == MaterialPhase::Solid ? TransportMedium::Solid : TransportMedium::Pipe;
    auto &eligible = assignments[material.item];
    for (size_t index = 0; index < ports.size(); ++index)
    {
        const auto &port = ports[index];
        if (port.type == medium &&
            (material.ports.empty() || std::find(material.ports.begin(), material.ports.end(),
                                                 port.source_index) != material.ports.end()))
            eligible.push_back(index);
    }
}
inline void add_factory_consumer(FactoryNetwork &network, std::map<std::string, int> &counts,
                                 const Facility &facility, const std::string &item, double rate,
                                 MaterialPhase phase)
{
    auto &entities = network.entities;
    FactoryEntity entity;
    entity.id = entities.size();
    entity.type = facility.name;
    entity.name = facility.name + "_" + std::to_string(++counts[facility.name]);
    entity.base_w = facility.width;
    entity.base_h = facility.height;
    entity.needs_power = facility.needs_power;
    entity.inputs = facility.inputs;
    entity.input_rates[item] = rate;
    bind_ports(RecipeIngredient{item, rate, phase, {}}, entity.inputs, entity.input_ports);
    entities.push_back(std::move(entity));
}

void add_pipe_distribution(const ProductionPlan &plan, FactoryNetwork &network,
                           std::map<std::string, int> &counts);
} // namespace aic
