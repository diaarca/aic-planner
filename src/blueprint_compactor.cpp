#include "blueprint_compactor.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace aic
{
namespace
{
using Cell = std::pair<int, int>;
using Clock = std::chrono::steady_clock;

int coordinate(const Cell &cell, int axis)
{
    return axis == 0 ? cell.first : cell.second;
}

void shift(Cell &cell, int axis, int line)
{
    auto &value = axis == 0 ? cell.first : cell.second;
    if (value > line)
        --value;
}

auto score(const BlueprintLayout &layout)
{
    return std::make_tuple(int64_t{layout.bounding_width} * layout.bounding_height,
                           layout.belt_tiles(), layout.bounding_width + layout.bounding_height);
}

std::array<std::set<int>, 2> protected_lines(const BlueprintLayout &layout,
                                             const std::vector<Cell> &approaches,
                                             const std::vector<StreamLink> &streams)
{
    std::array<std::set<int>, 2> result;
    for (int axis = 0; axis < 2; ++axis)
    {
        auto &lines = result[axis];
        std::map<std::string, std::pair<int, int>> modules;
        auto protect_module = [&](const std::string &id, int lo, int hi)
        {
            if (id.empty())
                return;
            const auto [it, inserted] = modules.emplace(id, std::make_pair(lo, hi));
            if (!inserted)
            {
                it->second.first = std::min(it->second.first, lo);
                it->second.second = std::max(it->second.second, hi);
            }
        };
        for (const auto &entity : layout.entities)
        {
            const int lo = axis == 0 ? entity.x : entity.y;
            const int hi = lo + (axis == 0 ? entity.width : entity.height) - 1;
            for (int k = lo; k <= hi; ++k)
                lines.insert(k);
            protect_module(entity.seed_loop, lo, hi);
        }
        for (const auto &cell : approaches)
            lines.insert(coordinate(cell, axis));
        for (const auto &cell : layout.conveyor_overpasses)
            for (int delta = -1; delta <= 1; ++delta)
                lines.insert(coordinate(cell, axis) + delta);
        for (size_t i = 0; i < layout.conveyor_paths.size(); ++i)
        {
            const auto &path = layout.conveyor_paths[i];
            lines.insert(coordinate(path[1], axis));
            lines.insert(coordinate(path[path.size() - 2], axis));
            if (!streams[i].fixed_path.empty())
                for (const auto &cell : path)
                {
                    const int k = coordinate(cell, axis);
                    lines.insert(k);
                    protect_module(layout.entities[streams[i].src_entity].seed_loop, k, k);
                }
        }
        for (const auto &[id, range] : modules)
            for (int k = range.first; k <= range.second; ++k)
                lines.insert(k);
    }
    return result;
}
} // namespace

void compact_routed_blueprint(BlueprintLayout &layout, const std::vector<StreamLink> &streams,
                              std::vector<Cell> approaches, Clock::time_point repair_deadline)
{
    auto normalize = [&](BlueprintLayout &current)
    {
        const auto [x, y] = current.normalize();
        for (auto &cell : approaches)
        {
            cell.first -= x;
            cell.second -= y;
        }
    };
    normalize(layout);
    auto assignments = streams;
    if (layout.conveyor_paths.size() != assignments.size())
        return;

    for (bool repair : {false, true})
    {
        while (!repair || (layout.conveyor_overpasses.empty() && Clock::now() < repair_deadline))
        {
            const auto protected_axis = protected_lines(layout, approaches, streams);
            BlueprintLayout best;
            std::vector<StreamLink> best_assignments;
            auto best_score = score(layout);
            int best_axis = 0, best_line = 0;
            Cell best_offset;
            for (int axis = 0; axis < 2; ++axis)
            {
                const int extent = axis == 0 ? layout.bounding_width : layout.bounding_height;
                for (int line = 1; line + 1 < extent; ++line)
                {
                    if (repair && Clock::now() >= repair_deadline)
                        break;
                    if (protected_axis[axis].count(line))
                        continue;

                    std::vector<bool> affected;
                    int repairs = 0;
                    for (const auto &path : layout.conveyor_paths)
                    {
                        bool needs_reroute = false;
                        for (size_t i = 1; i + 1 < path.size(); ++i)
                            if (coordinate(path[i], axis) == line &&
                                std::abs(coordinate(path[i - 1], axis) -
                                         coordinate(path[i + 1], axis)) != 2)
                            {
                                needs_reroute = true;
                                break;
                            }
                        affected.push_back(needs_reroute);
                        repairs += needs_reroute;
                    }
                    if ((!repair && repairs) || (repair && (repairs == 0 || repairs > 3)))
                        continue;

                    auto candidate = layout;
                    auto candidate_streams = assignments;
                    for (auto &entity : candidate.entities)
                    {
                        auto &origin = axis == 0 ? entity.x : entity.y;
                        if (origin > line)
                            --origin;
                        for (auto &port : entity.ports)
                        {
                            auto &p = axis == 0 ? port.x : port.y;
                            if (p > line)
                                --p;
                        }
                    }
                    for (size_t i = 0; i < candidate.conveyor_paths.size(); ++i)
                    {
                        auto &path = candidate.conveyor_paths[i];
                        path.erase(std::remove_if(path.begin(), path.end(),
                                                  [&](const Cell &cell)
                                                  { return coordinate(cell, axis) == line; }),
                                   path.end());
                        for (auto &cell : path)
                            shift(cell, axis, line);
                        if (repair)
                        {
                            auto &fixed = candidate_streams[i].fixed_path;
                            if (affected[i])
                                fixed.clear();
                            else
                                fixed.assign(path.begin() + 1, path.end() - 1);
                        }
                    }
                    if (repair)
                    {
                        // Hold all other belts in place while the affected belts negotiate.
                        const auto deadline =
                            std::min(repair_deadline, Clock::now() + std::chrono::milliseconds(35));
                        if (!route_conveyors(candidate, candidate_streams,
                                             layout.bounding_width - (axis == 0),
                                             layout.bounding_height - (axis == 1), deadline, false)
                                 .feasible)
                            continue;
                    }
                    else
                    {
                        candidate.conveyor_cells.clear();
                        candidate.conveyor_items.clear();
                        for (const auto &[cell, mask] : layout.conveyor_cells)
                        {
                            if (coordinate(cell, axis) == line)
                                continue;
                            auto moved = cell;
                            shift(moved, axis, line);
                            candidate.conveyor_cells.emplace(moved, mask);
                            candidate.conveyor_items.emplace(moved, layout.conveyor_items.at(cell));
                        }
                        for (auto &cell : candidate.conveyor_overpasses)
                            shift(cell, axis, line);
                    }

                    const auto offset = candidate.normalize();
                    if (score(candidate) < best_score)
                    {
                        best_score = score(candidate);
                        best_axis = axis;
                        best_line = line;
                        best_offset = offset;
                        best = std::move(candidate);
                        best_assignments = std::move(candidate_streams);
                    }
                }
            }
            if (best.entities.empty())
                break;
            for (auto &cell : approaches)
            {
                shift(cell, best_axis, best_line);
                cell.first -= best_offset.first;
                cell.second -= best_offset.second;
            }
            layout = std::move(best);
            assignments = std::move(best_assignments);
        }
    }
}
} // namespace aic
