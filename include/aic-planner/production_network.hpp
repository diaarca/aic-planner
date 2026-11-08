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
    std::string item_produced, item_consumed;
    const Recipe *recipe = nullptr;
    std::map<std::string, double> input_rates, output_rates;
    int dedicated_destination = -1;
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
};
FactoryNetwork build_factory_network(const ProductionPlan &plan, const std::string &target,
                                     const Facility &stash, const Facility &unloader);
} // namespace aic
