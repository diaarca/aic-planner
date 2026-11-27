#pragma once
#include "blueprint_layout.hpp"
#include <chrono>

namespace aic
{
struct StreamLink
{
    int src_entity = 0;
    int src_port_idx = 0;
    int dst_entity = 0;
    int dst_port_idx = 0;
    std::string item;
    double rate = 0;
    std::vector<std::pair<int, int>> fixed_path = {};
    MaterialPhase phase = MaterialPhase::Solid;
    bool fixed_ports = false;
};

struct RoutingResult
{
    bool feasible = false;
    std::vector<int> conflicted_stream_indices;
    std::vector<std::vector<int>> conflict_groups;
};
// Routes a placed blueprint; returns conflicts for the placement model's retry cuts.
RoutingResult route_conveyors(
    BlueprintLayout &layout, std::vector<StreamLink> &streams, int grid_width, int grid_height,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    bool allow_overpasses = true, bool allow_pipe_over_belt = false);
} // namespace aic
