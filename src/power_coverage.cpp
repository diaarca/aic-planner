#include "power_coverage.hpp"
#include "ortools/sat/cp_model.h"
#include <algorithm>
#include <set>

namespace aic
{
bool place_electric_pylons(BlueprintLayout &layout, const FactoryNetwork &network,
                           const Facility &pylon, int grid_width, int grid_height,
                           const std::vector<std::pair<int, int>> &reserved_cells)
{
    using namespace operations_research::sat;
    using Cell = std::pair<int, int>;
    const auto &range = pylon.power_range_extension.value();
    const int range_x = range.first, range_y = range.second;
    auto covers = [&](const PlacedBlueprintEntity &entity, int x, int y)
    {
        return x - range_x < entity.x + entity.width && entity.x < x + pylon.width + range_x &&
               y - range_y < entity.y + entity.height && entity.y < y + pylon.height + range_y;
    };

    std::vector<const PlacedBlueprintEntity *> uncovered;
    for (const auto &entity : network.entities)
    {
        const auto &placed = layout.entities[entity.id];
        if (entity.needs_power &&
            std::none_of(layout.entities.begin(), layout.entities.end(),
                         [&](const PlacedBlueprintEntity &other)
                         { return other.type == pylon.name && covers(placed, other.x, other.y); }))
            uncovered.push_back(&placed);
    }
    if (uncovered.empty())
        return true;

    std::set<Cell> occupied;
    for (const auto &entity : layout.entities)
        for (int y = entity.y; y < entity.y + entity.height; ++y)
            for (int x = entity.x; x < entity.x + entity.width; ++x)
                occupied.emplace(x, y);
    for (const auto &[cell, mask] : layout.conveyor_cells)
        occupied.insert(cell);
    const int min_x = occupied.begin()->first, max_x = occupied.rbegin()->first;
    const auto [lowest, highest] =
        std::minmax_element(occupied.begin(), occupied.end(),
                            [](const Cell &a, const Cell &b) { return a.second < b.second; });
    const int min_y = lowest->second, max_y = highest->second;
    occupied.insert(reserved_cells.begin(), reserved_cells.end());

    std::set<Cell> origins;
    for (const auto *entity : uncovered)
        for (int y = std::max(0, entity->y - pylon.height - range_y + 1);
             y <= std::min(grid_height - pylon.height, entity->y + entity->height + range_y - 1);
             ++y)
            for (int x = std::max(0, entity->x - pylon.width - range_x + 1);
                 x <= std::min(grid_width - pylon.width, entity->x + entity->width + range_x - 1);
                 ++x)
                origins.emplace(x, y);

    CpModelBuilder model;
    LinearExpr cost;
    std::map<Cell, BoolVar> candidates;
    std::map<Cell, std::vector<BoolVar>> by_tile;
    std::vector<std::vector<BoolVar>> by_facility(uncovered.size());
    for (const auto &[x, y] : origins)
    {
        std::vector<Cell> footprint;
        for (int dy = 0; dy < pylon.height; ++dy)
            for (int dx = 0; dx < pylon.width; ++dx)
                footprint.emplace_back(x + dx, y + dy);
        if (std::any_of(footprint.begin(), footprint.end(),
                        [&](const Cell &cell) { return occupied.count(cell); }))
            continue;

        const auto use = model.NewBoolVar();
        candidates.emplace(Cell{x, y}, use);
        for (const auto &cell : footprint)
            by_tile[cell].push_back(use);
        for (size_t i = 0; i < uncovered.size(); ++i)
            if (covers(*uncovered[i], x, y))
                by_facility[i].push_back(use);
        const bool expands_layout =
            x < min_x || y < min_y || x + pylon.width - 1 > max_x || y + pylon.height - 1 > max_y;
        cost += LinearExpr::Term(use, uncovered.size() + 1 + expands_layout);
    }
    for (const auto &choices : by_facility)
    {
        if (choices.empty())
            return false;
        model.AddBoolOr(choices);
    }
    for (const auto &[cell, choices] : by_tile)
        if (choices.size() > 1)
            model.AddAtMostOne(choices);
    model.Minimize(cost);

    SatParameters parameters;
    parameters.set_max_time_in_seconds(0.5);
    parameters.set_num_search_workers(1);
    Model solver;
    solver.Add(NewSatParameters(parameters));
    const auto solution = SolveCpModel(model.Build(), &solver);
    if (solution.status() == CpSolverStatus::MODEL_INVALID)
        throw std::runtime_error("Invalid power coverage model: " + solution.solution_info());
    if (solution.status() != CpSolverStatus::FEASIBLE &&
        solution.status() != CpSolverStatus::OPTIMAL)
        return false;

    int index = std::count_if(layout.entities.begin(), layout.entities.end(),
                              [&](const PlacedBlueprintEntity &entity)
                              { return entity.type == pylon.name; });
    for (const auto &[cell, use] : candidates)
        if (SolutionBooleanValue(solution, use))
        {
            PlacedBlueprintEntity entity;
            entity.type = pylon.name;
            entity.name = pylon.name + "_" + std::to_string(index + 1);
            entity.code = get_facility_code(pylon.name, index++);
            entity.color_code = get_facility_color(pylon.name);
            entity.x = cell.first;
            entity.y = cell.second;
            entity.width = pylon.width;
            entity.height = pylon.height;
            layout.entities.push_back(std::move(entity));
        }
    return true;
}
} // namespace aic
