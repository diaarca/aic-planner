#include "blueprint_solver.hpp"
#include "ortools/sat/cp_model.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <queue>

using namespace operations_research;
using namespace operations_research::sat;

BlueprintSolver::BlueprintSolver(const std::vector<Facility>& facilities,
                                 const std::vector<Mineral>& minerals,
                                 const std::vector<Area>& areas)
    : _facilities(facilities), _minerals(minerals), _areas(areas)
{
}

const Recipe*
BlueprintSolver::findRecipeFor(const std::string& item,
                               const Facility*& out_facility) const
{
    for (const auto& f : _facilities)
    {
        for (const auto& r : f.recipes)
        {
            for (const auto& p : r.products)
            {
                if (p.item == item)
                {
                    out_facility = &f;
                    return &r;
                }
            }
        }
    }
    out_facility = nullptr;
    return nullptr;
}

bool BlueprintSolver::isMineral(const std::string& item) const
{
    for (const auto& m : _minerals)
    {
        if (m.name == item)
        {
            return true;
        }
    }
    const Facility* dummy = nullptr;
    return (findRecipeFor(item, dummy) == nullptr);
}

void BlueprintLayout::print() const
{
    std::cout << "\n=======================================================\n";
    std::cout << "  BLUEPRINT LAYOUT: " << product_name << "\n";
    std::cout << "  Bounding Box: " << bounding_width << " x "
              << bounding_height
              << " (Area: " << (bounding_width * bounding_height) << ")\n";
    std::cout << "  Depot Length Required: " << depot_length << "\n";
    std::cout << "-------------------------------------------------------\n";
    std::cout << std::left << std::setw(25) << "Entity"
              << " | " << std::setw(8) << "Pos (X,Y)"
              << " | " << std::setw(8) << "Size"
              << " | " << std::setw(6) << "Rot"
              << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (const auto& e : entities)
    {
        std::string pos =
            "(" + std::to_string(e.x) + "," + std::to_string(e.y) + ")";
        std::string size =
            std::to_string(e.width) + "x" + std::to_string(e.height);
        std::cout << std::left << std::setw(25) << e.name << " | "
                  << std::setw(8) << pos << " | " << std::setw(8) << size
                  << " | " << e.rotation << "°\n";
    }
    std::cout << "=======================================================\n";
}

struct BlueprintEntityToPlace
{
    int id = 0;
    std::string type;
    std::string name;
    int base_w = 0;
    int base_h = 0;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    bool is_unloader = false;
    std::string item_produced;
    std::string item_consumed;
};

struct StreamLink
{
    int src_entity = 0;
    int src_port_idx = 0;
    int dst_entity = 0;
    int dst_port_idx = 0;
    std::string item;
    double rate = 0;
};

bool BlueprintSolver::solve(Product& product, BlueprintLayout& layout)
{
    const Facility* root_fac = nullptr;
    const Recipe* root_recipe = findRecipeFor(product.name, root_fac);
    if (!root_recipe || !root_fac)
    {
        return false;
    }

    double root_prod_amount = 1.0;
    for (const auto& p : root_recipe->products)
    {
        if (p.item == product.name)
        {
            root_prod_amount = p.amount;
            break;
        }
    }

    double root_time = root_recipe->time;
    double base_rate = root_prod_amount / root_time; // units per second

    product.production_time = root_time;
    product.mineral_consumption.clear();
    product.factory_facilities.clear();

    // Map of required item flow rates (items/s)
    std::map<std::string, double> item_flow_needed;
    for (const auto& ing : root_recipe->ingredients)
    {
        item_flow_needed[ing.item] += (ing.amount / root_time);
    }

    // Facilities needed count
    std::map<std::string, int> facility_counts;
    facility_counts[root_fac->name] += 1;

    // Mineral flow rates needed
    std::map<std::string, double> mineral_flows;

    // Intermediate facility instances
    struct IntermediateNode
    {
        const Facility* fac;
        const Recipe* recipe;
        int instance_count;
    };
    std::vector<IntermediateNode> intermediate_nodes;

    // Process upstream dependencies
    std::queue<std::pair<std::string, double>> q;
    for (const auto& [item, rate] : item_flow_needed)
    {
        q.push({item, rate});
    }

    while (!q.empty())
    {
        auto [item, rate] = q.front();
        q.pop();

        if (isMineral(item))
        {
            mineral_flows[item] += rate;
            continue;
        }

        const Facility* fac = nullptr;
        const Recipe* rec = findRecipeFor(item, fac);
        if (!rec || !fac)
        {
            mineral_flows[item] += rate;
            continue;
        }

        double out_amount = 1.0;
        for (const auto& p : rec->products)
        {
            if (p.item == item)
            {
                out_amount = p.amount;
                break;
            }
        }
        double single_rate = out_amount / rec->time;
        int count = static_cast<int>(std::ceil(rate / single_rate));

        facility_counts[fac->name] += count;
        intermediate_nodes.push_back({fac, rec, count});

        for (const auto& ing : rec->ingredients)
        {
            double ing_rate = (ing.amount / rec->time) * count;
            q.push({ing.item, ing_rate});
        }
    }

    // Mineral consumption per root batch
    for (const auto& [m, rate] : mineral_flows)
    {
        product.mineral_consumption[m] = rate * root_time;
    }

    // Count protocol stash
    facility_counts["protocol_stash"] += 1;
    for (const auto& [fname, cnt] : facility_counts)
    {
        product.factory_facilities[fname] = cnt;
    }

    // Build list of entities to place
    std::vector<BlueprintEntityToPlace> entities;
    int next_id = 0;

    // 1. Unloaders
    int total_unloaders = 0;
    std::map<std::string, std::vector<int>> unloader_ids_by_mineral;
    for (const auto& [m, rate] : mineral_flows)
    {
        int num_u = static_cast<int>(std::ceil(rate / 2.0));
        total_unloaders += num_u;
        for (int u = 0; u < num_u; ++u)
        {
            BlueprintEntityToPlace ent;
            ent.id = next_id++;
            ent.type = "unloader";
            ent.name = "unloader_" + m + "_" + std::to_string(u + 1);
            ent.base_w = 3;
            ent.base_h = 1;
            ent.is_unloader = true;
            ent.item_produced = m;
            ent.outputs.push_back(
                {1, 0, "product", 0, -1}); // middle output facing South
            unloader_ids_by_mineral[m].push_back(ent.id);
            entities.push_back(ent);
        }
    }

    // 2. Intermediate facilities
    std::map<std::string, std::vector<int>> fac_ids_by_produced_item;
    for (const auto& node : intermediate_nodes)
    {
        for (int c = 0; c < node.instance_count; ++c)
        {
            BlueprintEntityToPlace ent;
            ent.id = next_id++;
            ent.type = node.fac->name;
            ent.name = node.fac->name + "_" + std::to_string(c + 1);
            ent.base_w = node.fac->width;
            ent.base_h = node.fac->height;
            ent.inputs = node.fac->inputs;
            ent.outputs = node.fac->outputs;
            ent.item_produced = node.recipe->products[0].item;
            if (!node.recipe->ingredients.empty())
                ent.item_consumed = node.recipe->ingredients[0].item;

            fac_ids_by_produced_item[ent.item_produced].push_back(ent.id);
            entities.push_back(ent);
        }
    }

    // 3. Root facility
    int root_entity_id = next_id++;
    {
        BlueprintEntityToPlace ent;
        ent.id = root_entity_id;
        ent.type = root_fac->name;
        ent.name = root_fac->name + "_root";
        ent.base_w = root_fac->width;
        ent.base_h = root_fac->height;
        ent.inputs = root_fac->inputs;
        ent.outputs = root_fac->outputs;
        ent.item_produced = product.name;
        entities.push_back(ent);
    }

    // 4. Protocol Stash
    int stash_entity_id = next_id++;
    {
        BlueprintEntityToPlace ent;
        ent.id = stash_entity_id;
        ent.type = "protocol_stash";
        ent.name = "protocol_stash";
        ent.base_w = 3;
        ent.base_h = 3;
        ent.inputs.push_back({0, 0, "product", 0, -1});
        ent.inputs.push_back({1, 0, "product", 0, -1});
        ent.inputs.push_back({2, 0, "product", 0, -1});
        entities.push_back(ent);
    }

    // Streams to connect
    std::vector<StreamLink> streams;
    // Connect unloaders to intermediate facilities consuming mineral
    for (const auto& [m, u_ids] : unloader_ids_by_mineral)
    {
        for (auto& ent : entities)
        {
            if (ent.item_consumed == m && !ent.is_unloader)
            {
                int u_id = u_ids[0];
                streams.push_back({u_id, 0, ent.id, 0, m, 0.5});
            }
        }
        // Also check root facility
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == m)
            {
                streams.push_back({u_ids[0], 0, root_entity_id, 0, m, 0.5});
            }
        }
    }

    // Connect intermediate facilities to root facility or higher intermediate
    for (const auto& [item, f_ids] : fac_ids_by_produced_item)
    {
        // Check if root facility consumes it
        bool consumed_by_root = false;
        for (const auto& ing : root_recipe->ingredients)
        {
            if (ing.item == item)
            {
                consumed_by_root = true;
                break;
            }
        }
        if (consumed_by_root)
        {
            int p_idx = 0;
            for (int fid : f_ids)
            {
                streams.push_back(
                    {fid, 0, root_entity_id, p_idx % 6, item, 0.5});
                p_idx++;
            }
        }
        else
        {
            // Consumed by another intermediate
            for (auto& ent : entities)
            {
                if (ent.item_consumed == item && ent.id != root_entity_id &&
                    !ent.is_unloader)
                {
                    streams.push_back({f_ids[0], 0, ent.id, 0, item, 0.5});
                }
            }
        }
    }

    // Root facility to stash
    streams.push_back(
        {root_entity_id, 0, stash_entity_id, 1, product.name, base_rate});

    // Determine grid bounds from area
    int grid_w = 30;
    int grid_h = 30;
    if (!_areas.empty())
    {
        if (_areas[0].pac_width > 0)
            grid_w = static_cast<int>(_areas[0].pac_width);
        if (_areas[0].pac_height > 0)
            grid_h = static_cast<int>(_areas[0].pac_height);
    }

    // Build CP-SAT Model
    CpModelBuilder model;
    int N = entities.size();

    std::vector<IntVar> posX, posY, effW, effH;
    std::vector<std::vector<BoolVar>> rotB;
    std::vector<IntervalVar> intX, intY;

    for (int i = 0; i < N; ++i)
    {
        posX.push_back(model.NewIntVar(Domain(0, grid_w - 1))
                           .WithName(entities[i].name + "_x"));
        posY.push_back(model.NewIntVar(Domain(0, grid_h - 1))
                           .WithName(entities[i].name + "_y"));

        std::vector<BoolVar> b;
        for (int r = 0; r < 4; ++r)
        {
            b.push_back(model.NewBoolVar().WithName(entities[i].name + "_rot_" +
                                                    std::to_string(r)));
        }
        rotB.push_back(b);

        LinearExpr rot_sum;
        for (const auto& bv : b)
            rot_sum += bv;
        model.AddEquality(rot_sum, 1);

        int bw = entities[i].base_w;
        int bh = entities[i].base_h;

        // Unloaders attach to top depot: line Y = grid_h - 1, rotation 0
        if (entities[i].is_unloader)
        {
            model.AddEquality(b[0], 1);
            model.AddEquality(posY[i], grid_h - 1);
            model.AddLessOrEqual(posX[i], grid_w - bw);
        }

        IntVar w = model.NewIntVar(Domain(std::min(bw, bh), std::max(bw, bh)));
        IntVar h = model.NewIntVar(Domain(std::min(bw, bh), std::max(bw, bh)));

        LinearExpr expr_w =
            LinearExpr::Term(b[0], bw) + LinearExpr::Term(b[2], bw) +
            LinearExpr::Term(b[1], bh) + LinearExpr::Term(b[3], bh);
        LinearExpr expr_h =
            LinearExpr::Term(b[0], bh) + LinearExpr::Term(b[2], bh) +
            LinearExpr::Term(b[1], bw) + LinearExpr::Term(b[3], bw);

        model.AddEquality(w, expr_w);
        model.AddEquality(h, expr_h);

        effW.push_back(w);
        effH.push_back(h);

        IntVar end_x = model.NewIntVar(Domain(0, grid_w));
        IntVar end_y = model.NewIntVar(Domain(0, grid_h));
        model.AddEquality(end_x, posX[i] + w);
        model.AddEquality(end_y, posY[i] + h);

        intX.push_back(model.NewIntervalVar(posX[i], w, end_x));
        intY.push_back(model.NewIntervalVar(posY[i], h, end_y));
    }

    // Non-overlapping
    NoOverlap2DConstraint no_overlap = model.AddNoOverlap2D();
    for (size_t i = 0; i < intX.size(); ++i)
    {
        no_overlap.AddRectangle(intX[i], intY[i]);
    }

    // Port Access Tiles & Clearance
    struct PortAccessVar
    {
        int entity_id;
        bool is_input;
        int port_idx;
        IntVar ax;
        IntVar ay;
    };
    std::vector<PortAccessVar> port_accesses;

    for (int i = 0; i < N; ++i)
    {
        const auto& ent = entities[i];
        int bw = ent.base_w;
        int bh = ent.base_h;
        const auto& b = rotB[i];

        auto add_ports = [&](const std::vector<Port>& ports, bool is_input)
        {
            for (size_t p = 0; p < ports.size(); ++p)
            {
                const auto& port = ports[p];
                LinearExpr px = LinearExpr::Term(b[0], port.x) +
                                LinearExpr::Term(b[1], port.y) +
                                LinearExpr::Term(b[2], bw - 1 - port.x) +
                                LinearExpr::Term(b[3], bh - 1 - port.y);
                LinearExpr py = LinearExpr::Term(b[0], port.y) +
                                LinearExpr::Term(b[1], bw - 1 - port.x) +
                                LinearExpr::Term(b[2], bh - 1 - port.y) +
                                LinearExpr::Term(b[3], port.x);

                LinearExpr fx = LinearExpr::Term(b[0], port.fx) +
                                LinearExpr::Term(b[1], port.fy) -
                                LinearExpr::Term(b[2], port.fx) -
                                LinearExpr::Term(b[3], port.fy);
                LinearExpr fy = LinearExpr::Term(b[0], port.fy) -
                                LinearExpr::Term(b[1], port.fx) -
                                LinearExpr::Term(b[2], port.fy) +
                                LinearExpr::Term(b[3], port.fx);

                IntVar ax = model.NewIntVar(Domain(0, grid_w - 1));
                IntVar ay = model.NewIntVar(Domain(0, grid_h - 1));
                model.AddEquality(ax, posX[i] + px + fx);
                model.AddEquality(ay, posY[i] + py + fy);

                // Port access cell must NOT be covered by any other entity j !=
                // i
                for (int j = 0; j < N; ++j)
                {
                    if (j == i)
                        continue;
                    BoolVar left = model.NewBoolVar();
                    BoolVar right = model.NewBoolVar();
                    BoolVar below = model.NewBoolVar();
                    BoolVar above = model.NewBoolVar();

                    model.AddLessOrEqual(ax, posX[j] - 1).OnlyEnforceIf(left);
                    model.AddGreaterOrEqual(ax, posX[j] + effW[j])
                        .OnlyEnforceIf(right);
                    model.AddLessOrEqual(ay, posY[j] - 1).OnlyEnforceIf(below);
                    model.AddGreaterOrEqual(ay, posY[j] + effH[j])
                        .OnlyEnforceIf(above);

                    model.AddBoolOr({left, right, below, above});
                }
                port_accesses.push_back(
                    {i, is_input, static_cast<int>(p), ax, ay});
            }
        };

        add_ports(ent.inputs, true);
        add_ports(ent.outputs, false);
    }

    // Bounding Box variables
    IntVar max_x = model.NewIntVar(Domain(0, grid_w)).WithName("max_x");
    IntVar max_y = model.NewIntVar(Domain(0, grid_h)).WithName("max_y");
    IntVar min_x = model.NewIntVar(Domain(0, grid_w)).WithName("min_x");
    IntVar min_y = model.NewIntVar(Domain(0, grid_h)).WithName("min_y");

    for (int i = 0; i < N; ++i)
    {
        model.AddGreaterOrEqual(max_x, posX[i] + effW[i]);
        model.AddGreaterOrEqual(max_y, posY[i] + effH[i]);
        model.AddLessOrEqual(min_x, posX[i]);
        model.AddLessOrEqual(min_y, posY[i]);
    }

    IntVar bbox_w = model.NewIntVar(Domain(1, grid_w)).WithName("bbox_w");
    IntVar bbox_h = model.NewIntVar(Domain(1, grid_h)).WithName("bbox_h");
    model.AddEquality(bbox_w, max_x - min_x);
    model.AddEquality(bbox_h, max_y - min_y);

    IntVar area = model.NewIntVar(Domain(1, grid_w * grid_h)).WithName("area");
    model.AddMultiplicationEquality(area, bbox_w, bbox_h);

    // Minimize Manhattan conveyor distances as regularization
    LinearExpr stream_dist;
    for (const auto& s : streams)
    {
        auto find_access = [&](int ent_id, bool is_in,
                               int p_idx) -> const PortAccessVar*
        {
            for (const auto& pa : port_accesses)
            {
                if (pa.entity_id == ent_id && pa.is_input == is_in &&
                    pa.port_idx == p_idx)
                {
                    return &pa;
                }
            }
            return nullptr;
        };

        const auto* src_pa = find_access(s.src_entity, false, s.src_port_idx);
        const auto* dst_pa = find_access(s.dst_entity, true, s.dst_port_idx);
        if (src_pa && dst_pa)
        {
            IntVar dx = model.NewIntVar(Domain(0, grid_w));
            IntVar dy = model.NewIntVar(Domain(0, grid_h));
            model.AddAbsEquality(dx, src_pa->ax - dst_pa->ax);
            model.AddAbsEquality(dy, src_pa->ay - dst_pa->ay);
            stream_dist += dx + dy;
        }
    }

    // Objective
    model.Minimize(area * 100 + bbox_w * 10 + bbox_h * 10 + stream_dist);

    SatParameters params;
    params.set_max_time_in_seconds(10.0);
    params.set_num_search_workers(8);

    Model cp_model;
    cp_model.Add(NewSatParameters(params));
    CpSolverResponse response = SolveCpModel(model.Build(), &cp_model);

    if (response.status() != CpSolverStatus::OPTIMAL &&
        response.status() != CpSolverStatus::FEASIBLE)
    {
        return false;
    }

    product.factory_width = SolutionIntegerValue(response, bbox_w);
    product.factory_height = SolutionIntegerValue(response, bbox_h);
    product.factory_depot = total_unloaders * 3;

    layout.product_name = product.name;
    layout.bounding_width = static_cast<int>(product.factory_width);
    layout.bounding_height = static_cast<int>(product.factory_height);
    layout.depot_length = static_cast<int>(product.factory_depot);
    layout.entities.clear();

    for (int i = 0; i < N; ++i)
    {
        PlacedBlueprintEntity pe;
        pe.name = entities[i].name;
        pe.type = entities[i].type;
        pe.x = SolutionIntegerValue(response, posX[i]);
        pe.y = SolutionIntegerValue(response, posY[i]);
        pe.width = SolutionIntegerValue(response, effW[i]);
        pe.height = SolutionIntegerValue(response, effH[i]);
        pe.is_unloader = entities[i].is_unloader;
        pe.mineral = entities[i].item_produced;

        int rot = 0;
        for (int r = 0; r < 4; ++r)
        {
            if (SolutionBooleanValue(response, rotB[i][r]))
            {
                rot = r * 90;
                break;
            }
        }
        pe.rotation = rot;
        layout.entities.push_back(pe);
    }

    return true;
}
