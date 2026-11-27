#pragma once
#include "conveyor_router.hpp"
#include "recipe_graph.hpp"

namespace aic
{
struct FactoryEntity
{
    int id = 0;
    std::string type, name;
    int base_w = 0, base_h = 0, base_rotation = 0;
    std::vector<Port> inputs, outputs;
    bool is_unloader = false;
    bool needs_power = false;
    std::string item_produced, item_consumed;
    const Recipe *recipe = nullptr;
    std::map<std::string, double> input_rates, output_rates;
    std::map<std::string, std::vector<int>> input_ports, output_ports;
    const AreaSource *site = nullptr;
    const AreaClimate *climate = nullptr;
    int climate_provider = -1;
    bool climate_feed = false;
    int activation_port = -1;
    const ActivationFeed *activation = nullptr;
    const ArrayField *array_field = nullptr;
    const ArrayProjector *projection = nullptr;
    int projection_target = -1;
    double pipe_limit_per_minute = 0;
    double cycle_seconds = 0;
    int dedicated_destination = -1;
    std::map<std::string, std::set<int>> pipe_destinations;
    std::string seed_loop;
    SeedLoopRole loop_role = SeedLoopRole::None;
    int loop_partner = -1;
    int loop_harvest = -1;
};
struct FactoryNetwork
{
    std::vector<FactoryEntity> entities;
    std::vector<StreamLink> streams;
    int root_entity_id = -1, stash_entity_id = -1;
    int total_unloaders = 0, unloader_width = 0;
    const DepotBusPiece *delivery = nullptr;
};
FactoryNetwork build_factory_network(const ProductionPlan &plan, const std::string &target,
                                     const Facility &stash, const Facility &unloader,
                                     const DepotBusPiece *delivery = nullptr);
} // namespace aic
