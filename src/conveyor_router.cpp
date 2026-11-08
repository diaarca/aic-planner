#include "conveyor_router.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <set>

namespace aic
{
namespace
{
// Match each port bank to neighboring machines without changing its active ports.
void normalize_port_order(const BlueprintLayout &layout, std::vector<StreamLink> &streams,
                          bool input)
{
    std::map<int, std::vector<size_t>> banks;
    for (size_t index = 0; index < streams.size(); ++index)
        banks[input ? streams[index].dst_entity : streams[index].src_entity].push_back(index);
    for (const auto &[entity, indices] : banks)
    {
        if (entity < 0 || entity >= static_cast<int>(layout.entities.size()) || indices.size() < 2)
            continue;
        if (std::any_of(indices.begin(), indices.end(),
                        [&](size_t index) { return !streams[index].fixed_path.empty(); }))
            continue;
        std::set<int> assigned;
        for (const auto index : indices)
            assigned.insert(input ? streams[index].dst_port_idx : streams[index].src_port_idx);
        std::vector<std::pair<PlacedPort, int>> ports;
        int port_index = 0;
        for (const auto &port : layout.entities[entity].ports)
            if (port.is_input == input)
            {
                if (assigned.count(port_index))
                    ports.push_back({port, port_index});
                ++port_index;
            }
        if (ports.size() != indices.size())
            continue;
        const bool horizontal = std::abs(ports.back().first.x - ports.front().first.x) >=
                                std::abs(ports.back().first.y - ports.front().first.y);
        std::sort(ports.begin(), ports.end(),
                  [&](const auto &a, const auto &b)
                  { return horizontal ? a.first.x < b.first.x : a.first.y < b.first.y; });
        auto order = indices;
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b)
                  {
                      const auto &first =
                          layout.entities[input ? streams[a].src_entity : streams[a].dst_entity];
                      const auto &second =
                          layout.entities[input ? streams[b].src_entity : streams[b].dst_entity];
                      const int x = horizontal ? first.x : first.y,
                                y = horizontal ? second.x : second.y;
                      return x == y ? a < b : x < y;
                  });
        for (size_t index = 0; index < order.size(); ++index)
        {
            auto &stream = streams[order[index]];
            (input ? stream.dst_port_idx : stream.src_port_idx) = ports[index].second;
        }
    }
}
} // namespace

RoutingResult route_conveyors(BlueprintLayout &layout, std::vector<StreamLink> &streams, int grid_w,
                              int grid_h)
{
    // -------------------------------------------------------------
    // PathFinder Negotiated Congestion Conveyor Router
    // -------------------------------------------------------------
    layout.conveyor_cells.clear();
    layout.conveyor_items.clear();
    layout.is_conveyor_feasible = true;

    std::set<std::pair<int, int>> facility_cells;
    for (const auto &pe : layout.entities)
    {
        for (int dy = 0; dy < pe.height; ++dy)
        {
            for (int dx = 0; dx < pe.width; ++dx)
            {
                facility_cells.insert({pe.x + dx, pe.y + dy});
            }
        }
    }

    auto get_dir_mask = [](int dx, int dy) -> int
    {
        if (dy == 1)
            return CONV_NORTH;
        if (dy == -1)
            return CONV_SOUTH;
        if (dx == 1)
            return CONV_EAST;
        if (dx == -1)
            return CONV_WEST;
        return 0;
    };

    auto get_dir_from_delta = [](int dx, int dy) -> int
    {
        if (dy == 1)
            return 0; // North
        if (dy == -1)
            return 1; // South
        if (dx == 1)
            return 2; // East
        if (dx == -1)
            return 3; // West
        return -1;
    };

    struct StreamEndpoints
    {
        PlacedPort src_port;
        PlacedPort dst_port;
        std::pair<int, int> start_pt;
        std::pair<int, int> target_pt;
        int dir_from_src_port;
        int dir_to_dst_port;
        bool valid;
    };

    std::vector<StreamEndpoints> stream_endpoints(streams.size());
    std::set<std::pair<int, int>> all_port_access_cells;

    normalize_port_order(layout, streams, false);
    normalize_port_order(layout, streams, true);

    for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
    {
        const auto &s = streams[s_idx];
        if (s.src_entity < 0 || s.dst_entity < 0 ||
            s.src_entity >= static_cast<int>(layout.entities.size()) ||
            s.dst_entity >= static_cast<int>(layout.entities.size()))
        {
            stream_endpoints[s_idx].valid = false;
            continue;
        }

        const auto &pe_src = layout.entities[s.src_entity];
        std::vector<PlacedPort> src_outputs;
        for (const auto &p : pe_src.ports)
        {
            if (!p.is_input)
                src_outputs.push_back(p);
        }
        if (s.src_port_idx < 0 || s.src_port_idx >= static_cast<int>(src_outputs.size()))
        {
            stream_endpoints[s_idx].valid = false;
            continue;
        }
        const auto &src_port = src_outputs[s.src_port_idx];

        const auto &pe_dst = layout.entities[s.dst_entity];
        std::vector<PlacedPort> dst_inputs;
        for (const auto &p : pe_dst.ports)
        {
            if (p.is_input)
                dst_inputs.push_back(p);
        }
        if (s.dst_port_idx < 0 || s.dst_port_idx >= static_cast<int>(dst_inputs.size()))
        {
            stream_endpoints[s_idx].valid = false;
            continue;
        }
        const auto &dst_port = dst_inputs[s.dst_port_idx];

        int src_ax = src_port.x + src_port.fx;
        int src_ay = src_port.y + src_port.fy;
        int dst_ax = dst_port.x + dst_port.fx;
        int dst_ay = dst_port.y + dst_port.fy;

        if (src_ax < 0 || src_ay < 0 || src_ax >= grid_w || src_ay >= grid_h || dst_ax < 0 ||
            dst_ay < 0 || dst_ax >= grid_w || dst_ay >= grid_h)
        {
            stream_endpoints[s_idx].valid = false;
            continue;
        }

        int d_from_src = get_dir_from_delta(src_port.fx, src_port.fy);
        int d_to_dst = get_dir_from_delta(-dst_port.fx, -dst_port.fy);

        stream_endpoints[s_idx] = {
            src_port, dst_port, {src_ax, src_ay}, {dst_ax, dst_ay}, d_from_src, d_to_dst, true};
        all_port_access_cells.insert({src_ax, src_ay});
        all_port_access_cells.insert({dst_ax, dst_ay});
    }

    struct PathNode
    {
        int x, y, layer; // 0: Ground, 1: Overpass Bridge
    };

    struct OccSegment
    {
        int stream_idx;
        int dir_in;
        int dir_out;
        bool is_straight;
    };

    const int W = grid_w;
    const int H = grid_h;
    // cell_occ[W][H][2]: layer 0 = Ground, layer 1 = Bridge
    std::vector<std::vector<std::vector<std::vector<OccSegment>>>> cell_occ(
        W, std::vector<std::vector<std::vector<OccSegment>>>(
               H, std::vector<std::vector<OccSegment>>(2)));
    std::vector<std::vector<int>> cell_history_penalty(W, std::vector<int>(H, 0));

    auto is_conflicted = [&](int x, int y) -> bool
    {
        const auto &occ0 = cell_occ[x][y][0];
        const auto &occ1 = cell_occ[x][y][1];
        if (occ0.size() > 1 || occ1.size() > 1)
            return true;
        if (occ0.empty() || occ1.empty())
            return false;
        // Both layers have 1 stream: valid if and only if both are straight and perpendicular
        if (!occ0[0].is_straight || !occ1[0].is_straight)
            return true;
        bool v0 = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
        bool v1 = (occ1[0].dir_in == 0 || occ1[0].dir_in == 1);
        return (v0 ==
                v1); // If same orientation, conflicted! If different, valid perpendicular bridge
    };

    std::vector<std::vector<PathNode>> stream_paths(streams.size());

    auto add_path_to_occ = [&](int s_idx)
    {
        const auto &path = stream_paths[s_idx];
        if (path.empty())
            return;
        int m = static_cast<int>(path.size()) - 1;
        int d_from_src = stream_endpoints[s_idx].dir_from_src_port;
        int d_to_dst = stream_endpoints[s_idx].dir_to_dst_port;

        for (int k = 0; k <= m; ++k)
        {
            int d_in =
                (k == 0) ? d_from_src
                         : get_dir_from_delta(path[k].x - path[k - 1].x, path[k].y - path[k - 1].y);
            int d_out =
                (k == m) ? d_to_dst
                         : get_dir_from_delta(path[k + 1].x - path[k].x, path[k + 1].y - path[k].y);
            bool straight = (d_in == d_out);
            cell_occ[path[k].x][path[k].y][path[k].layer].push_back({s_idx, d_in, d_out, straight});
        }
    };

    auto remove_path_from_occ = [&](int s_idx)
    {
        const auto &path = stream_paths[s_idx];
        for (const auto &node : path)
        {
            auto &occ = cell_occ[node.x][node.y][node.layer];
            occ.erase(std::remove_if(occ.begin(), occ.end(),
                                     [&](const OccSegment &seg)
                                     { return seg.stream_idx == s_idx; }),
                      occ.end());
        }
    };

    std::map<std::pair<int, int>, int> reserved_paths;
    for (size_t index = 0; index < streams.size(); ++index)
        for (const auto &cell : streams[index].fixed_path)
            if (!reserved_paths.emplace(cell, index).second)
            {
                layout.is_conveyor_feasible = false;
                return {false, {static_cast<int>(index)}, {}};
            }
    auto blocked = [&](int x, int y, int stream)
    {
        const auto found = reserved_paths.find({x, y});
        return facility_cells.count({x, y}) ||
               (found != reserved_paths.end() && found->second != stream);
    };

    auto route_stream = [&](int s_idx, double p_k) -> bool
    {
        const auto &ep = stream_endpoints[s_idx];
        if (!ep.valid)
            return false;

        std::pair<int, int> start_pt = ep.start_pt;
        std::pair<int, int> target_pt = ep.target_pt;
        int d_from_src = ep.dir_from_src_port;
        int d_to_dst = ep.dir_to_dst_port;

        if (!streams[s_idx].fixed_path.empty())
        {
            const auto &fixed = streams[s_idx].fixed_path;
            if (fixed.front() != start_pt || fixed.back() != target_pt)
                return false;
            for (size_t index = 0; index < fixed.size(); ++index)
            {
                const auto [x, y] = fixed[index];
                if (x < 0 || y < 0 || x >= grid_w || y >= grid_h || blocked(x, y, s_idx))
                    return false;
                if (index > 0 &&
                    std::abs(x - fixed[index - 1].first) + std::abs(y - fixed[index - 1].second) !=
                        1)
                    return false;
            }
            stream_paths[s_idx].clear();
            for (const auto &[x, y] : fixed)
                stream_paths[s_idx].push_back({x, y, 0});
            add_path_to_occ(s_idx);
            return true;
        }

        if (start_pt == target_pt)
        {
            stream_paths[s_idx] = {{start_pt.first, start_pt.second, 0}};
            add_path_to_occ(s_idx);
            return true;
        }

        struct QNode
        {
            int cost;
            int x, y, layer, dir;
            bool operator>(const QNode &other) const
            {
                return cost > other.cost;
            }
        };

        const int INF = 1e9;
        // dist[W][H][2][4]
        std::vector<std::vector<std::vector<std::vector<int>>>> dist(
            W, std::vector<std::vector<std::vector<int>>>(
                   H, std::vector<std::vector<int>>(2, std::vector<int>(4, INF))));

        struct Parent
        {
            int x, y, layer, dir;
        };
        std::vector<std::vector<std::vector<std::vector<Parent>>>> parent(
            W,
            std::vector<std::vector<std::vector<Parent>>>(
                H, std::vector<std::vector<Parent>>(2, std::vector<Parent>(4, {-1, -1, -1, -1}))));

        std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> pq;

        dist[start_pt.first][start_pt.second][0][d_from_src] = 0;
        pq.push({0, start_pt.first, start_pt.second, 0, d_from_src});

        const int dxy[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}}; // 0: N, 1: S, 2: E, 3: W

        int best_target_cost = INF;
        int best_target_dir = -1;

        while (!pq.empty())
        {
            auto [c, x, y, layer, dir] = pq.top();
            pq.pop();

            if (c > dist[x][y][layer][dir])
                continue;
            if (c >= best_target_cost)
                break;

            // Docking at target: must be on Ground (layer 0)
            if (x == target_pt.first && y == target_pt.second && layer == 0)
            {
                int turn_p = (dir != d_to_dst) ? 20 : 0;
                int total_c = c + turn_p;
                if (total_c < best_target_cost)
                {
                    best_target_cost = total_c;
                    best_target_dir = dir;
                }
                continue;
            }

            if (layer == 0)
            {
                // 1. Move on Ground (Layer 0)
                for (int next_d = 0; next_d < 4; ++next_d)
                {
                    if (next_d == (dir ^ 1))
                        continue; // Cannot 180 reverse

                    int nx = x + dxy[next_d][0];
                    int ny = y + dxy[next_d][1];

                    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                        continue;
                    if (blocked(nx, ny, s_idx))
                        continue;

                    int turn_cost = (next_d != dir) ? 15 : 0;
                    int h_cost = cell_history_penalty[nx][ny];

                    int occ_cost = 0;
                    const auto &occ0 = cell_occ[nx][ny][0];
                    if (!occ0.empty())
                    {
                        occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                    }
                    if (all_port_access_cells.count({nx, ny}) &&
                        std::pair<int, int>{nx, ny} != target_pt)
                    {
                        occ_cost += 500;
                    }

                    int step_cost = 10;
                    int nc = c + step_cost + turn_cost + h_cost + occ_cost;
                    if (nc < dist[nx][ny][0][next_d])
                    {
                        dist[nx][ny][0][next_d] = nc;
                        parent[nx][ny][0][next_d] = {x, y, layer, dir};
                        pq.push({nc, nx, ny, 0, next_d});
                    }
                }

                // 2. Ramp Up to Overpass Bridge (Layer 1)
                // Must continue straight ahead in current direction 'dir'
                {
                    int nx = x + dxy[dir][0];
                    int ny = y + dxy[dir][1];

                    if (nx >= 0 && nx < W && ny >= 0 && ny < H && !blocked(nx, ny, s_idx) &&
                        std::pair<int, int>{nx, ny} != target_pt)
                    {
                        int h_cost = cell_history_penalty[nx][ny];
                        int occ_cost = 0;
                        const auto &occ1 = cell_occ[nx][ny][1];
                        if (!occ1.empty())
                        {
                            occ_cost = static_cast<int>((1500.0 + 50.0 * h_cost) * p_k);
                        }
                        else
                        {
                            const auto &occ0 = cell_occ[nx][ny][0];
                            if (!occ0.empty())
                            {
                                bool is_v = (dir == 0 || dir == 1);
                                bool other_v = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
                                if (occ0[0].is_straight && (is_v != other_v))
                                {
                                    occ_cost = 0; // Valid perpendicular bridge over Layer 0!
                                }
                                else
                                {
                                    occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                                }
                            }
                        }

                        int bridge_step_cost = 30; // base (10) + bridge hardware (20)
                        int nc = c + bridge_step_cost + h_cost + occ_cost;
                        if (nc < dist[nx][ny][1][dir])
                        {
                            dist[nx][ny][1][dir] = nc;
                            parent[nx][ny][1][dir] = {x, y, layer, dir};
                            pq.push({nc, nx, ny, 1, dir});
                        }
                    }
                }
            }
            else // layer == 1 (Currently on Overpass Bridge)
            {
                // Physical rule: Bridges CANNOT turn! Must continue straight ahead in direction
                // 'dir'.
                int nx = x + dxy[dir][0];
                int ny = y + dxy[dir][1];

                if (nx >= 0 && nx < W && ny >= 0 && ny < H && !blocked(nx, ny, s_idx))
                {
                    int h_cost = cell_history_penalty[nx][ny];

                    // Choice A: Ramp Down to Ground (Layer 0)
                    {
                        int occ_cost = 0;
                        const auto &occ0 = cell_occ[nx][ny][0];
                        if (!occ0.empty())
                        {
                            occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                        }
                        if (all_port_access_cells.count({nx, ny}) &&
                            std::pair<int, int>{nx, ny} != target_pt)
                        {
                            occ_cost += 500;
                        }

                        int step_cost = 10;
                        int nc = c + step_cost + h_cost + occ_cost;
                        if (nc < dist[nx][ny][0][dir])
                        {
                            dist[nx][ny][0][dir] = nc;
                            parent[nx][ny][0][dir] = {x, y, layer, dir};
                            pq.push({nc, nx, ny, 0, dir});
                        }
                    }

                    // Choice B: Continue on Bridge (Layer 1) over another parallel belt
                    if (std::pair<int, int>{nx, ny} != target_pt)
                    {
                        int occ_cost = 0;
                        const auto &occ1 = cell_occ[nx][ny][1];
                        if (!occ1.empty())
                        {
                            occ_cost = static_cast<int>((1500.0 + 50.0 * h_cost) * p_k);
                        }
                        else
                        {
                            const auto &occ0 = cell_occ[nx][ny][0];
                            if (!occ0.empty())
                            {
                                bool is_v = (dir == 0 || dir == 1);
                                bool other_v = (occ0[0].dir_in == 0 || occ0[0].dir_in == 1);
                                if (occ0[0].is_straight && (is_v != other_v))
                                {
                                    occ_cost = 0;
                                }
                                else
                                {
                                    occ_cost = static_cast<int>((1000.0 + 50.0 * h_cost) * p_k);
                                }
                            }
                        }

                        int bridge_step_cost = 30;
                        int nc = c + bridge_step_cost + h_cost + occ_cost;
                        if (nc < dist[nx][ny][1][dir])
                        {
                            dist[nx][ny][1][dir] = nc;
                            parent[nx][ny][1][dir] = {x, y, layer, dir};
                            pq.push({nc, nx, ny, 1, dir});
                        }
                    }
                }
            }
        }

        if (best_target_dir == -1)
        {
            return false;
        }

        std::vector<PathNode> path;
        int cx = target_pt.first;
        int cy = target_pt.second;
        int clayer = 0;
        int cdir = best_target_dir;

        while (true)
        {
            path.push_back({cx, cy, clayer});
            if (cx == start_pt.first && cy == start_pt.second && clayer == 0 && cdir == d_from_src)
            {
                break;
            }
            Parent p = parent[cx][cy][clayer][cdir];
            cx = p.x;
            cy = p.y;
            clayer = p.layer;
            cdir = p.dir;
        }
        std::reverse(path.begin(), path.end());
        stream_paths[s_idx] = path;
        add_path_to_occ(s_idx);
        return true;
    };

    // Initial routing pass with soft congestion (p_0 = 1.0)
    double p_k = 1.0;
    bool all_initial_routed = true;
    for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
    {
        if (!route_stream(static_cast<int>(s_idx), p_k))
        {
            all_initial_routed = false;
        }
    }

    bool pf_feasible = false;
    std::vector<int> conflicted_stream_indices;
    std::set<std::pair<int, int>> final_conflicted_cells;

    const int max_pf_iters = 12;
    for (int iter = 0; iter < max_pf_iters; ++iter)
    {
        std::set<std::pair<int, int>> conflicted_cells;
        std::vector<int> cur_conflicted_streams;

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (is_conflicted(x, y))
                {
                    conflicted_cells.insert({x, y});
                    for (const auto &occ : cell_occ[x][y][0])
                        cur_conflicted_streams.push_back(occ.stream_idx);
                    for (const auto &occ : cell_occ[x][y][1])
                        cur_conflicted_streams.push_back(occ.stream_idx);
                }
            }
        }

        for (size_t index = 0; index < stream_paths.size(); ++index)
            if (stream_paths[index].empty())
                cur_conflicted_streams.push_back(static_cast<int>(index));

        if (conflicted_cells.empty() && all_initial_routed)
        {
            pf_feasible = true;
            break;
        }

        std::sort(cur_conflicted_streams.begin(), cur_conflicted_streams.end());
        cur_conflicted_streams.erase(
            std::unique(cur_conflicted_streams.begin(), cur_conflicted_streams.end()),
            cur_conflicted_streams.end());

        conflicted_stream_indices = cur_conflicted_streams;
        final_conflicted_cells = conflicted_cells;

        // Increase historical penalty on conflicted cells
        for (const auto &[cx, cy] : conflicted_cells)
        {
            cell_history_penalty[cx][cy] += 35;
        }

        // Exponential VPR schedule for present congestion: p_{k+1} = min(25.0, 1.3 * p_k)
        p_k = std::min(25.0, p_k * 1.3);

        // Slack-based rip-up sorting (Recommendation 4):
        // On even iterations: sort ascending by slack (critical tight streams claim lane first).
        // On odd iterations: random shuffle to break cyclic locks.
        if (iter % 2 == 0)
        {
            std::sort(cur_conflicted_streams.begin(), cur_conflicted_streams.end(),
                      [&](int a, int b)
                      {
                          int dist_a = std::abs(stream_endpoints[a].start_pt.first -
                                                stream_endpoints[a].target_pt.first) +
                                       std::abs(stream_endpoints[a].start_pt.second -
                                                stream_endpoints[a].target_pt.second);
                          int dist_b = std::abs(stream_endpoints[b].start_pt.first -
                                                stream_endpoints[b].target_pt.first) +
                                       std::abs(stream_endpoints[b].start_pt.second -
                                                stream_endpoints[b].target_pt.second);
                          int slack_a = static_cast<int>(stream_paths[a].size()) - dist_a;
                          int slack_b = static_cast<int>(stream_paths[b].size()) - dist_b;
                          return slack_a < slack_b;
                      });
        }
        else
        {
            std::mt19937 rng(42 + iter);
            std::shuffle(cur_conflicted_streams.begin(), cur_conflicted_streams.end(), rng);
        }

        // Synchronous rip-up: remove ALL conflicted streams FIRST
        for (int s_idx : cur_conflicted_streams)
        {
            remove_path_from_occ(s_idx);
        }

        // Immediately reroute each conflicted stream one by one
        bool reroute_all = true;
        for (int s_idx : cur_conflicted_streams)
        {
            if (!route_stream(s_idx, p_k))
            {
                reroute_all = false;
            }
        }
        all_initial_routed =
            reroute_all && std::all_of(stream_paths.begin(), stream_paths.end(),
                                       [](const auto &path) { return !path.empty(); });
    }

    if (pf_feasible)
    {
        // Assemble conveyor cells and items
        for (size_t s_idx = 0; s_idx < streams.size(); ++s_idx)
        {
            const auto &s = streams[s_idx];
            const auto &path = stream_paths[s_idx];
            const auto &ep = stream_endpoints[s_idx];
            int m = static_cast<int>(path.size()) - 1;

            for (int k = 0; k <= m; ++k)
            {
                auto pt = std::make_pair(path[k].x, path[k].y);
                int mask = 0;
                if (k == 0)
                {
                    int to_src_dx = -ep.src_port.fx;
                    int to_src_dy = -ep.src_port.fy;
                    mask |= get_dir_mask(to_src_dx, to_src_dy);
                }
                else
                {
                    int prev_dx = path[k - 1].x - pt.first;
                    int prev_dy = path[k - 1].y - pt.second;
                    mask |= get_dir_mask(prev_dx, prev_dy);
                }

                if (k == m)
                {
                    int to_dst_dx = -ep.dst_port.fx;
                    int to_dst_dy = -ep.dst_port.fy;
                    mask |= get_dir_mask(to_dst_dx, to_dst_dy);
                }
                else
                {
                    int next_dx = path[k + 1].x - pt.first;
                    int next_dy = path[k + 1].y - pt.second;
                    mask |= get_dir_mask(next_dx, next_dy);
                }

                layout.conveyor_cells[pt] |= mask;
                layout.conveyor_items[pt] = s.item;
            }

            // Assign item to src and dst ports for color visualization
            for (auto &p : layout.entities[s.src_entity].ports)
            {
                if (p.x == ep.src_port.x && p.y == ep.src_port.y)
                {
                    p.item = s.item;
                    break;
                }
            }
            for (auto &p : layout.entities[s.dst_entity].ports)
            {
                if (p.x == ep.dst_port.x && p.y == ep.dst_port.y)
                {
                    p.item = s.item;
                    break;
                }
            }
        }
    }
    layout.is_conveyor_feasible = pf_feasible;
    RoutingResult result;
    result.feasible = pf_feasible;
    result.conflicted_stream_indices = conflicted_stream_indices;
    for (const auto &[x, y] : final_conflicted_cells)
    {
        std::vector<int> indices;
        for (int layer = 0; layer < 2; ++layer)
            for (const auto &occupant : cell_occ[x][y][layer])
                indices.push_back(occupant.stream_idx);
        result.conflict_groups.push_back(std::move(indices));
    }
    return result;
}
} // namespace aic
