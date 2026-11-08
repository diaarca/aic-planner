#include "plant_module.hpp"
#include <stdexcept>

namespace aic
{
std::map<int, int> add_plant_pylons(FactoryNetwork &network, const RecipeGraph &graph)
{
    auto &entities = network.entities;
    std::map<int, int> module_pylons;
    const size_t production_entities = entities.size();
    for (size_t index = 0; index < production_entities; ++index)
        if (entities[index].loop_role == SeedLoopRole::Picker && entities[index].loop_harvest >= 0)
        {
            auto check_banks = [](const FactoryEntity &entity)
            {
                if (entity.base_w != 5 || entity.base_h != 5 || entity.inputs.size() != 5 ||
                    entity.outputs.size() != 5)
                    throw std::invalid_argument(
                        "Plant template requires 5x5 machines with five solid ports on each bank");
                for (int port = 0; port < 5; ++port)
                {
                    const auto &in = entity.inputs[port];
                    const auto &out = entity.outputs[port];
                    if (in.x != port || in.y != 0 || in.fx != 0 || in.fy != -1 || out.x != port ||
                        out.y != 4 || out.fx != 0 || out.fy != 1)
                        throw std::invalid_argument(
                            "Plant template port geometry has changed; update the fixed layout");
                }
            };
            check_banks(entities[index]);
            check_banks(entities[entities[index].loop_partner]);
            check_banks(entities[entities[index].loop_harvest]);
            const auto &pylon = graph.facility("power_diffuser_1");
            if (pylon.width != 2 || pylon.height != 2)
                throw std::invalid_argument("Plant template requires a 2x2 electric pylon");
            FactoryEntity pole;
            pole.id = entities.size();
            pole.type = pylon.name;
            pole.name = pylon.name + "_" + std::to_string(module_pylons.size() + 1);
            pole.base_w = 2;
            pole.base_h = 2;
            pole.seed_loop = entities[index].seed_loop;
            pole.loop_role = SeedLoopRole::Pylon;
            pole.loop_partner = index;
            module_pylons[index] = pole.id;
            entities.push_back(pole);
        }
    return module_pylons;
}

void fix_plant_paths(FactoryNetwork &network, const BlueprintLayout &layout, bool standalone)
{
    const auto &entities = network.entities;
    for (auto &stream : network.streams)
    {
        stream.fixed_path.clear();
        const auto &source = entities[stream.src_entity];
        const auto &destination = entities[stream.dst_entity];
        if (source.loop_role == SeedLoopRole::Picker && source.loop_harvest >= 0)
        {
            const auto &left = layout.entities[source.loop_partner];
            const int x = left.x, y = left.y;
            stream.fixed_path =
                destination.loop_role == SeedLoopRole::Planter
                    ? std::vector<std::pair<int, int>>{{x + 5, y + 5}, {x + 4, y + 5}}
                    : std::vector<std::pair<int, int>>{{x + 9, y + 5}, {x + 10, y + 5}};
        }
        if (source.loop_role == SeedLoopRole::Planter && source.loop_harvest >= 0)
        {
            const auto &left = layout.entities[source.id];
            stream.fixed_path = {{left.x + 4, left.y - 1}, {left.x + 5, left.y - 1}};
        }
        if (standalone && source.loop_role == SeedLoopRole::Harvest)
        {
            const auto &right = layout.entities[source.id];
            stream.fixed_path = {{right.x, right.y - 1}, {right.x - 1, right.y - 1}};
        }
    }
}
} // namespace aic
