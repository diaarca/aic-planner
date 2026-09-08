#include "solver.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>

using namespace operations_research;
using namespace operations_research::sat;

Solver::Solver(const std::vector<Product>& products,
               const std::vector<Mineral>& mineral_limits,
               const std::vector<Area>& areas,
               const std::vector<Fuel>& fuels,
               const std::map<std::string, double>& facility_power,
               const Region& region)
    : _products(products), _mineral_limits(mineral_limits), _areas(areas),
      _fuels(fuels), _facility_power(facility_power), _region(region),
      _obj_scale_factor(60)
{
}

void Solver::setBlueprintLayouts(
    const std::map<std::string, BlueprintLayout>& layouts)
{
    _product_layouts = layouts;
}

void Solver::solve()
{
    // Compute scaling factor for fuel duration to keep all coefficients integer
    int64_t scale = 1;
    for (const auto& fuel : _fuels)
    {
        if (fuel.duration > 0)
        {
            scale = std::lcm(scale, static_cast<int64_t>(fuel.duration));
        }
    }
    _obj_scale_factor = scale;

    instantiateVariables();
    declareConstraints();

    if (solveModel())
    {
        displaySolution();
    }
    else
    {
        std::cerr << "No solution found." << std::endl;
    }
}

void Solver::instantiateVariables()
{
    // Limit the production of products to not overflow the regional storage
    const int64_t max_production =
        std::ceil(_region.storage / 60 / NB_HOUR_BEFORE_OVERFLOW);

    _qty_produced.clear();
    for (size_t p = 0; p < _products.size(); ++p)
    {
        _qty_produced.push_back(_cp_model.NewIntVar(Domain(0, max_production))
                                    .WithName(_products[p].name + "_prod"));
    }

    _factories_in_area.clear();
    _factories_in_area.resize(_products.size());
    for (size_t p = 0; p < _products.size(); ++p)
    {
        for (size_t a = 0; a < _areas.size(); ++a)
        {
            std::string name = _products[p].name + "_" + _areas[a].name;
            // Limit the number of facotries to not overflow the regional
            // storage
            const int64_t max_factory =
                std::ceil(max_production * _products[p].production_time / 60);
            _factories_in_area[p].push_back(
                _cp_model.NewIntVar(Domain(0, max_factory)).WithName(name));
        }
    }

    _num_batteries_active.clear();
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        _num_batteries_active.push_back(
            _cp_model.NewIntVar(Domain(0, max_production))
                .WithName(_fuels[fu].name + "_active"));
    }

    _area_slots.clear();
    _area_slots.resize(_areas.size());
    if (!_product_layouts.empty())
    {
        for (size_t a = 0; a < _areas.size(); ++a)
        {
            int W_a = _areas[a].pac_width;
            int H_a = _areas[a].pac_height;
            if (W_a <= 0 || H_a <= 0)
                continue;

            int K = 8;
            for (int k = 0; k < K; ++k)
            {
                AreaSlotVar slot;
                std::string prefix =
                    "a" + std::to_string(a) + "_s" + std::to_string(k);
                slot.active = _cp_model.NewBoolVar().WithName(prefix + "_act");
                for (size_t p = 0; p < _products.size(); ++p)
                {
                    slot.is_prod.push_back(_cp_model.NewBoolVar().WithName(
                        prefix + "_p" + std::to_string(p)));
                }
                slot.is_rot[0] =
                    _cp_model.NewBoolVar().WithName(prefix + "_r0");
                slot.is_rot[90] =
                    _cp_model.NewBoolVar().WithName(prefix + "_r90");
                slot.is_rot[180] =
                    _cp_model.NewBoolVar().WithName(prefix + "_r180");
                slot.is_rot[270] =
                    _cp_model.NewBoolVar().WithName(prefix + "_r270");

                slot.bx =
                    _cp_model.NewIntVar(Domain(0, W_a)).WithName(prefix + "_bx");
                slot.by =
                    _cp_model.NewIntVar(Domain(0, H_a)).WithName(prefix + "_by");
                slot.bw =
                    _cp_model.NewIntVar(Domain(0, W_a)).WithName(prefix + "_bw");
                slot.bh =
                    _cp_model.NewIntVar(Domain(0, H_a)).WithName(prefix + "_bh");

                _area_slots[a].push_back(slot);
            }
        }
    }
}

void Solver::declareConstraints()
{
    // Instanciate fuel map for easier lookup
    std::map<std::string, size_t> fuel_map;
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        fuel_map[_fuels[fu].name] = fu;
    }

    // Map of product name to product index
    std::map<std::string, size_t> product_map;
    for (size_t p = 0; p < _products.size(); ++p)
    {
        product_map[_products[p].name] = p;
    }

    // Objective: Maximize total net value (sold products)
    LinearExpr objective;

    for (size_t p = 0; p < _products.size(); ++p)
    {
        objective += LinearExpr::Term(
            _qty_produced[p],
            _obj_scale_factor * static_cast<int64_t>(_products[p].value));
    }

    // Fuel Balance Constraint for ALL fuels
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        const std::string& fuel_name = _fuels[fu].name;
        if (product_map.count(fuel_name))
        {
            size_t p = product_map.at(fuel_name);
            int64_t d_fu = static_cast<int64_t>(_fuels[fu].duration);
            int64_t cost_coeff_fu = (_obj_scale_factor * 60 / d_fu) *
                                    static_cast<int64_t>(_products[p].value);
            objective +=
                LinearExpr::Term(_num_batteries_active[fu], -cost_coeff_fu);

            LinearExpr c_fuel_bal;
            c_fuel_bal += LinearExpr::Term(_qty_produced[p], d_fu);
            c_fuel_bal += LinearExpr::Term(_num_batteries_active[fu], -60);
            _cp_model.AddGreaterOrEqual(c_fuel_bal, 0);
        }
        else
        {
            // Fuel is not produced at all: cannot have active batteries!
            _cp_model.AddEquality(_num_batteries_active[fu], 0);
        }
    }
    _cp_model.Maximize(objective);

    // Storage Capacity Constraint
    for (size_t p = 0; p < _products.size(); ++p)
    {
        if (fuel_map.count(_products[p].name))
        {
            size_t fu = fuel_map.at(_products[p].name);
            int64_t d_fu = static_cast<int64_t>(_fuels[fu].duration);

            LinearExpr c_storage_capa;
            c_storage_capa += LinearExpr::Term(_qty_produced[p], d_fu);
            c_storage_capa += LinearExpr::Term(_num_batteries_active[fu], -60);
            int64_t max_storage = static_cast<int64_t>(
                (_region.storage / (NB_HOUR_BEFORE_OVERFLOW * 60.0)) * d_fu);
            _cp_model.AddLessOrEqual(c_storage_capa, max_storage);
        }
        else
        {
            int64_t max_storage = static_cast<int64_t>(
                _region.storage / (NB_HOUR_BEFORE_OVERFLOW * 60.0));
            _cp_model.AddLessOrEqual(_qty_produced[p], max_storage);
        }
    }

    // Mineral limits
    for (size_t m = 0; m < _mineral_limits.size(); ++m)
    {
        const std::string& mineral_name = _mineral_limits[m].name;
        int64_t mineral_limit = static_cast<int64_t>(_mineral_limits[m].limit);

        LinearExpr c_mineral;
        for (size_t p = 0; p < _products.size(); ++p)
        {
            if (_products[p].mineral_consumption.count(mineral_name))
            {
                c_mineral += LinearExpr::Term(
                    _qty_produced[p],
                    static_cast<int64_t>(
                        _products[p].mineral_consumption.at(mineral_name)));
            }
        }
        _cp_model.AddLessOrEqual(c_mineral, mineral_limit);
    }

    // Factory Capacity Constraint
    for (size_t p = 0; p < _products.size(); ++p)
    {
        LinearExpr c_nb_factory;
        c_nb_factory += LinearExpr::Term(
            _qty_produced[p],
            static_cast<int64_t>(_products[p].production_time));
        for (size_t a = 0; a < _areas.size(); ++a)
        {
            c_nb_factory += LinearExpr::Term(_factories_in_area[p][a], -60);
        }
        _cp_model.AddLessOrEqual(c_nb_factory, 0);
    }

    // Area Space and Depot Constraint
    for (size_t a = 0; a < _areas.size(); ++a)
    {
        double total_available_area =
            _areas[a].pac_width * _areas[a].pac_height;
        if (total_available_area > 0)
        {
            if (!_product_layouts.empty() && !_area_slots[a].empty())
            {
                int W_a = _areas[a].pac_width;
                int H_a = _areas[a].pac_height;

                bool has_top_depot = false;
                int top_x1 = 0, top_x2 = W_a - 1;
                bool has_right_depot = false;
                int right_y1 = 0, right_y2 = H_a - 1;
                bool has_bottom_depot = false;
                int bot_x1 = 0, bot_x2 = W_a - 1;
                bool has_left_depot = false;
                int left_y1 = 0, left_y2 = H_a - 1;

                for (const auto& d : _areas[a].static_depots)
                {
                    if (d.y1 == H_a && d.y2 == H_a)
                    {
                        has_top_depot = true;
                        top_x1 = std::min(d.x1, d.x2);
                        top_x2 = std::max(d.x1, d.x2);
                    }
                    if (d.x1 == W_a && d.x2 == W_a)
                    {
                        has_right_depot = true;
                        right_y1 = std::min(d.y1, d.y2);
                        right_y2 = std::max(d.y1, d.y2);
                    }
                    if (d.y1 == 0 && d.y2 == 0)
                    {
                        has_bottom_depot = true;
                        bot_x1 = std::min(d.x1, d.x2);
                        bot_x2 = std::max(d.x1, d.x2);
                    }
                    if (d.x1 == 0 && d.x2 == 0)
                    {
                        has_left_depot = true;
                        left_y1 = std::min(d.y1, d.y2);
                        left_y2 = std::max(d.y1, d.y2);
                    }
                }

                std::vector<IntervalVar> x_intervals;
                std::vector<IntervalVar> y_intervals;

                for (size_t k = 0; k < _area_slots[a].size(); ++k)
                {
                    auto& slot = _area_slots[a][k];
                    _cp_model.AddEquality(LinearExpr::Sum(slot.is_prod),
                                          slot.active);

                    LinearExpr rot_sum;
                    rot_sum += slot.is_rot[0];
                    rot_sum += slot.is_rot[90];
                    rot_sum += slot.is_rot[180];
                    rot_sum += slot.is_rot[270];
                    _cp_model.AddEquality(rot_sum, slot.active);

                    if (!has_top_depot)
                        _cp_model.AddEquality(slot.is_rot[0], 0);
                    if (!has_right_depot)
                        _cp_model.AddEquality(slot.is_rot[90], 0);
                    if (!has_bottom_depot)
                        _cp_model.AddEquality(slot.is_rot[180], 0);
                    if (!has_left_depot)
                        _cp_model.AddEquality(slot.is_rot[270], 0);

                    LinearExpr bw_expr;
                    LinearExpr bh_expr;
                    for (size_t p = 0; p < _products.size(); ++p)
                    {
                        const auto& layout =
                            _product_layouts.at(_products[p].name);
                        int bw0 = layout.bounding_width;
                        int bh0 = layout.bounding_height;

                        for (int r : {0, 90, 180, 270})
                        {
                            int cur_w = (r == 0 || r == 180) ? bw0 : bh0;
                            int cur_h = (r == 0 || r == 180) ? bh0 : bw0;

                            BoolVar is_pr = _cp_model.NewBoolVar();
                            _cp_model.AddBoolAnd({slot.is_prod[p], slot.is_rot[r]})
                                .OnlyEnforceIf(is_pr);
                            _cp_model.AddBoolOr({slot.is_prod[p].Not(),
                                                 slot.is_rot[r].Not()})
                                .OnlyEnforceIf(is_pr.Not());

                            bw_expr += LinearExpr::Term(is_pr, cur_w);
                            bh_expr += LinearExpr::Term(is_pr, cur_h);
                        }
                    }
                    _cp_model.AddEquality(slot.bw, bw_expr);
                    _cp_model.AddEquality(slot.bh, bh_expr);

                    _cp_model.AddLessOrEqual(slot.bx + slot.bw, W_a);
                    _cp_model.AddLessOrEqual(slot.by + slot.bh, H_a);

                    if (has_top_depot)
                    {
                        _cp_model.AddEquality(slot.by + slot.bh, H_a)
                            .OnlyEnforceIf(slot.is_rot[0]);
                        _cp_model.AddGreaterOrEqual(slot.bx, top_x1)
                            .OnlyEnforceIf(slot.is_rot[0]);
                        _cp_model.AddLessOrEqual(slot.bx + slot.bw - 1, top_x2)
                            .OnlyEnforceIf(slot.is_rot[0]);
                    }
                    if (has_right_depot)
                    {
                        _cp_model.AddEquality(slot.bx + slot.bw, W_a)
                            .OnlyEnforceIf(slot.is_rot[90]);
                        _cp_model.AddGreaterOrEqual(slot.by, right_y1)
                            .OnlyEnforceIf(slot.is_rot[90]);
                        _cp_model.AddLessOrEqual(slot.by + slot.bh - 1, right_y2)
                            .OnlyEnforceIf(slot.is_rot[90]);
                    }
                    if (has_bottom_depot)
                    {
                        _cp_model.AddEquality(slot.by, 0)
                            .OnlyEnforceIf(slot.is_rot[180]);
                        _cp_model.AddGreaterOrEqual(slot.bx, bot_x1)
                            .OnlyEnforceIf(slot.is_rot[180]);
                        _cp_model.AddLessOrEqual(slot.bx + slot.bw - 1, bot_x2)
                            .OnlyEnforceIf(slot.is_rot[180]);
                    }
                    if (has_left_depot)
                    {
                        _cp_model.AddEquality(slot.bx, 0)
                            .OnlyEnforceIf(slot.is_rot[270]);
                        _cp_model.AddGreaterOrEqual(slot.by, left_y1)
                            .OnlyEnforceIf(slot.is_rot[270]);
                        _cp_model.AddLessOrEqual(slot.by + slot.bh - 1, left_y2)
                            .OnlyEnforceIf(slot.is_rot[270]);
                    }

                    IntVar end_x = _cp_model.NewIntVar(Domain(0, W_a));
                    IntVar end_y = _cp_model.NewIntVar(Domain(0, H_a));
                    _cp_model.AddEquality(end_x, slot.bx + slot.bw);
                    _cp_model.AddEquality(end_y, slot.by + slot.bh);
                    IntervalVar ix = _cp_model.NewOptionalIntervalVar(
                        slot.bx, slot.bw, end_x, slot.active);
                    IntervalVar iy = _cp_model.NewOptionalIntervalVar(
                        slot.by, slot.bh, end_y, slot.active);
                    x_intervals.push_back(ix);
                    y_intervals.push_back(iy);

                    if (k + 1 < _area_slots[a].size())
                    {
                        _cp_model.AddGreaterOrEqual(
                            slot.active, _area_slots[a][k + 1].active);
                    }
                }

                NoOverlap2DConstraint no_overlap = _cp_model.AddNoOverlap2D();
                for (size_t i = 0; i < x_intervals.size(); ++i)
                {
                    no_overlap.AddRectangle(x_intervals[i], y_intervals[i]);
                }

                for (size_t p = 0; p < _products.size(); ++p)
                {
                    LinearExpr total_p;
                    for (size_t k = 0; k < _area_slots[a].size(); ++k)
                    {
                        total_p += _area_slots[a][k].is_prod[p];
                    }
                    _cp_model.AddEquality(_factories_in_area[p][a], total_p);
                }
            }
            else
            {
                // Total space
                LinearExpr c_space;
                for (size_t i = 0; i < _products.size(); ++i)
                {
                    int64_t factory_area = static_cast<int64_t>(
                        _products[i].factory_width * _products[i].factory_height);
                    c_space +=
                        LinearExpr::Term(_factories_in_area[i][a], factory_area);
                }
                _cp_model.AddLessOrEqual(
                    c_space, static_cast<int64_t>(total_available_area));

                // Depot constraint
                LinearExpr c_depot;
                for (size_t i = 0; i < _products.size(); ++i)
                {
                    int64_t factory_depot =
                        static_cast<int64_t>(_products[i].factory_depot);
                    c_depot +=
                        LinearExpr::Term(_factories_in_area[i][a], factory_depot);
                }
                int64_t total_depot_in_area = static_cast<int64_t>(
                    _areas[a].pac_depot_width + _areas[a].pac_depot_height);
                _cp_model.AddLessOrEqual(c_depot, total_depot_in_area);
            }
        }
        else
        {
            for (size_t p = 0; p < _products.size(); ++p)
            {
                _cp_model.AddEquality(_factories_in_area[p][a], 0);
            }
        }
    }

    // Power Consumption Constraint
    int64_t static_demand = 0;
    for (const auto& area : _areas)
    {
        if (area.area_facilities.count("zipline") &&
            _facility_power.count("zipline"))
        {
            static_demand +=
                static_cast<int64_t>(area.area_facilities.at("zipline") *
                                     _facility_power.at("zipline"));
        }
        if (area.area_facilities.count("defense") &&
            _facility_power.count("defense"))
        {
            static_demand +=
                static_cast<int64_t>(area.area_facilities.at("defense") *
                                     _facility_power.at("defense"));
        }
        if (area.area_facilities.count("mining_rig") &&
            _facility_power.count("mining_rig"))
        {
            static_demand +=
                static_cast<int64_t>(area.area_facilities.at("mining_rig") *
                                     _facility_power.at("mining_rig"));
        }
    }

    LinearExpr c_power_cons;
    for (size_t p = 0; p < _products.size(); ++p)
    {
        int64_t factory_power = 0;
        for (const auto& facility : _products[p].factory_facilities)
        {
            if (_facility_power.count(facility.first))
                factory_power += static_cast<int64_t>(
                    facility.second * _facility_power.at(facility.first));
        }
        if (factory_power > 0)
        {
            for (size_t a = 0; a < _areas.size(); ++a)
                c_power_cons +=
                    LinearExpr::Term(_factories_in_area[p][a], factory_power);
        }
    }
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        c_power_cons += LinearExpr::Term(
            _num_batteries_active[fu], -static_cast<int64_t>(_fuels[fu].power));
    }

    int64_t max_power_allowed =
        static_cast<int64_t>(_region.base_power) - static_demand;
    _cp_model.AddLessOrEqual(c_power_cons, max_power_allowed);
}

bool Solver::solveModel()
{
    SatParameters parameters;
    parameters.set_log_search_progress(false);
    parameters.set_num_workers(8);

    Model model;
    model.Add(NewSatParameters(parameters));

    _response = SolveCpModel(_cp_model.Build(), &model);
    return _response.status() == CpSolverStatus::OPTIMAL ||
           _response.status() == CpSolverStatus::FEASIBLE;
}

void Solver::displaySolution()
{
    std::cout << "Solution Status: OPTIMAL" << std::endl;
    std::cout << "Optimal Objective Value (Net Value per Minute): "
              << (_response.objective_value() /
                  static_cast<double>(_obj_scale_factor))
              << std::endl;

    std::cout << "\n--- Production Plan (units per minute) ---" << std::endl;
    for (size_t p = 0; p < _products.size(); ++p)
    {
        int64_t produced = SolutionIntegerValue(_response, _qty_produced[p]);
        if (produced > 0)
        {
            int64_t total_factories = 0;
            for (size_t a = 0; a < _areas.size(); ++a)
                total_factories +=
                    SolutionIntegerValue(_response, _factories_in_area[p][a]);
            std::cout << _products[p].name << ": " << produced << " units ["
                      << total_factories << " factories]" << std::endl;
        }
    }

    std::cout << "\n--- Factory Placement ---" << std::endl;
    for (size_t a = 0; a < _areas.size(); ++a)
    {
        bool area_used = false;
        for (size_t p = 0; p < _products.size(); ++p)
        {
            if (SolutionIntegerValue(_response, _factories_in_area[p][a]) > 0)
            {
                area_used = true;
                break;
            }
        }
        if (area_used)
        {
            std::cout << "Area: " << _areas[a].name << std::endl;
            double used_space = 0;
            double used_depot = 0;
            for (size_t p = 0; p < _products.size(); ++p)
            {
                int64_t num_f =
                    SolutionIntegerValue(_response, _factories_in_area[p][a]);
                if (num_f > 0)
                {
                    std::cout << "  - " << _products[p].name << ": " << num_f
                              << " factories" << std::endl;
                    used_space += num_f * (_products[p].factory_width *
                                           _products[p].factory_height);
                    used_depot += num_f * _products[p].factory_depot;
                }
            }
            std::cout << "  Space used: " << used_space << " / "
                      << (_areas[a].pac_width * _areas[a].pac_height)
                      << std::endl;

            std::cout << "  Depot length used: " << used_depot << " / "
                      << _areas[a].pac_depot_width + _areas[a].pac_depot_height
                      << std::endl;
        }
    }

    std::cout << "\n--- Mineral Consumption (usage / limit) ---" << std::endl;
    for (const auto& mineral : _mineral_limits)
    {
        double total_consumed = 0.0;
        for (size_t p = 0; p < _products.size(); ++p)
        {
            if (_products[p].mineral_consumption.count(mineral.name))
                total_consumed +=
                    _products[p].mineral_consumption.at(mineral.name) *
                    SolutionIntegerValue(_response, _qty_produced[p]);
        }
        for (size_t fu = 0; fu < _fuels.size(); ++fu)
        {
            if (_fuels[fu].name == mineral.name)
                total_consumed +=
                    SolutionIntegerValue(_response, _num_batteries_active[fu]) *
                    (60.0 / _fuels[fu].duration);
        }
        std::cout << mineral.name << ": " << total_consumed << " / "
                  << mineral.limit << std::endl;
    }

    std::cout << "\n--- Power Consumption ---" << std::endl;
    double p_zip = 0, p_def = 0, p_mine = 0, p_fact = 0;
    for (const auto& area : _areas)
    {
        if (area.area_facilities.count("zipline") &&
            _facility_power.count("zipline"))
            p_zip += area.area_facilities.at("zipline") *
                     _facility_power.at("zipline");

        if (area.area_facilities.count("defense") &&
            _facility_power.count("defense"))
            p_def += area.area_facilities.at("defense") *
                     _facility_power.at("defense");

        if (area.area_facilities.count("mining_rig") &&
            _facility_power.count("mining_rig"))
            p_mine += area.area_facilities.at("mining_rig") *
                      _facility_power.at("mining_rig");
    }
    for (size_t p = 0; p < _products.size(); ++p)
    {
        double f_pow = 0;
        for (const auto& facility : _products[p].factory_facilities)
            if (_facility_power.count(facility.first))
                f_pow += facility.second * _facility_power.at(facility.first);
        for (size_t a = 0; a < _areas.size(); ++a)
            p_fact +=
                SolutionIntegerValue(_response, _factories_in_area[p][a]) *
                f_pow;
    }
    std::cout << "Power for Ziplines: " << p_zip
              << "\nPower for Defenses: " << p_def
              << "\nPower for Mining Rigs: " << p_mine
              << "\nPower for Factories: " << p_fact << std::endl;
    std::cout << "Total Power Needed: " << (p_zip + p_def + p_fact)
              << std::endl;

    std::cout << "\n--- Power Production (Optimal Battery Mix) ---"
              << std::endl;
    double t_supp = 0;
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        int64_t num =
            SolutionIntegerValue(_response, _num_batteries_active[fu]);
        if (num > 0)
        {
            double s = num * _fuels[fu].power;
            std::cout << _fuels[fu].name << ": " << num << " active batteries ("
                      << s << " power, " << num * (60.0 / _fuels[fu].duration)
                      << " units/min consumption)" << std::endl;
            t_supp += s;
        }
    }
    std::cout << "Total Power Provided: " << t_supp << std::endl;

    if (!_product_layouts.empty())
    {
        displayAreaLayouts();
    }
}

void Solver::displayAreaLayouts()
{
    for (size_t a = 0; a < _areas.size(); ++a)
    {
        int W_a = _areas[a].pac_width;
        int H_a = _areas[a].pac_height;
        if (W_a <= 0 || H_a <= 0)
            continue;

        struct CellInfo
        {
            int bg = -1;
            int fg = 16;
            std::string text = " ·";
        };
        std::map<std::pair<int, int>, CellInfo> grid;
        std::map<std::pair<int, int>, int> conveyor_cells;
        std::map<std::pair<int, int>, std::string> conveyor_items;

        int active_count = 0;
        std::vector<std::string> placed_summary;

        if (a < _area_slots.size())
        {
            for (size_t k = 0; k < _area_slots[a].size(); ++k)
            {
                const auto& slot = _area_slots[a][k];
                if (!SolutionBooleanValue(_response, slot.active))
                    continue;

                active_count++;
                int p_idx = -1;
                for (size_t p = 0; p < _products.size(); ++p)
                {
                    if (SolutionBooleanValue(_response, slot.is_prod[p]))
                    {
                        p_idx = p;
                        break;
                    }
                }
                if (p_idx < 0)
                    continue;

                int rot = 0;
                for (int r : {0, 90, 180, 270})
                {
                    if (SolutionBooleanValue(_response, slot.is_rot.at(r)))
                    {
                        rot = r;
                        break;
                    }
                }

                int bx = SolutionIntegerValue(_response, slot.bx);
                int by = SolutionIntegerValue(_response, slot.by);
                int bw = SolutionIntegerValue(_response, slot.bw);
                int bh = SolutionIntegerValue(_response, slot.bh);

                const auto& layout = _product_layouts.at(_products[p_idx].name);
                int W_0 = layout.bounding_width;
                int H_0 = layout.bounding_height;

                placed_summary.push_back(_products[p_idx].name + " [" +
                                         std::to_string(bw) + "x" +
                                         std::to_string(bh) + " @ (" +
                                         std::to_string(bx) + "," +
                                         std::to_string(by) + ") rot:" +
                                         std::to_string(rot) + "°]");

                // Place entities
                for (const auto& entity : layout.entities)
                {
                    int ex = 0, ey = 0, ew = 0, eh = 0;
                    if (rot == 0)
                    {
                        ex = entity.x;
                        ey = entity.y;
                        ew = entity.width;
                        eh = entity.height;
                    }
                    else if (rot == 90)
                    {
                        ex = entity.y;
                        ey = W_0 - (entity.x + entity.width);
                        ew = entity.height;
                        eh = entity.width;
                    }
                    else if (rot == 180)
                    {
                        ex = W_0 - (entity.x + entity.width);
                        ey = H_0 - (entity.y + entity.height);
                        ew = entity.width;
                        eh = entity.height;
                    }
                    else if (rot == 270)
                    {
                        ex = H_0 - (entity.y + entity.height);
                        ey = entity.x;
                        ew = entity.height;
                        eh = entity.width;
                    }

                    int gx = bx + ex;
                    int gy = by + ey;
                    int cx = gx + ew / 2;
                    int cy = gy + eh / 2;
                    int fg = (entity.color_code == 33 ||
                              entity.color_code == 160 ||
                              entity.color_code == 34)
                                 ? 15
                                 : 16;

                    for (int dy = 0; dy < eh; ++dy)
                    {
                        for (int dx = 0; dx < ew; ++dx)
                        {
                            int cur_x = gx + dx;
                            int cur_y = gy + dy;
                            CellInfo ci;
                            ci.bg = entity.color_code;
                            ci.fg = fg;
                            ci.text = "  ";
                            if (cur_x == cx && cur_y == cy)
                            {
                                ci.text = entity.code;
                                if (ci.text.size() == 1)
                                    ci.text += " ";
                            }
                            grid[{cur_x, cur_y}] = ci;
                        }
                    }

                    // Place ports
                    for (const auto& p : entity.ports)
                    {
                        int rpx = 0, rpy = 0, rfx = 0, rfy = 0;
                        if (rot == 0)
                        {
                            rpx = p.x;
                            rpy = p.y;
                            rfx = p.fx;
                            rfy = p.fy;
                        }
                        else if (rot == 90)
                        {
                            rpx = p.y;
                            rpy = W_0 - 1 - p.x;
                            rfx = p.fy;
                            rfy = -p.fx;
                        }
                        else if (rot == 180)
                        {
                            rpx = W_0 - 1 - p.x;
                            rpy = H_0 - 1 - p.y;
                            rfx = -p.fx;
                            rfy = -p.fy;
                        }
                        else if (rot == 270)
                        {
                            rpx = H_0 - 1 - p.y;
                            rpy = p.x;
                            rfx = -p.fy;
                            rfy = p.fx;
                        }

                        int port_gx = bx + rpx;
                        int port_gy = by + rpy;
                        CellInfo& pci = grid[{port_gx, port_gy}];
                        if (!p.item.empty())
                        {
                            pci.fg = getItemColor(p.item);
                        }
                        if (p.is_input)
                        {
                            if (rfy == 1)
                                pci.text = "▼ ";
                            else if (rfy == -1)
                                pci.text = "▲ ";
                            else if (rfx == 1)
                                pci.text = "◄ ";
                            else if (rfx == -1)
                                pci.text = "► ";
                        }
                        else
                        {
                            if (rfy == 1)
                                pci.text = "▲ ";
                            else if (rfy == -1)
                                pci.text = "▼ ";
                            else if (rfx == 1)
                                pci.text = "► ";
                            else if (rfx == -1)
                                pci.text = "◄ ";
                        }
                    }
                }

                // Place conveyors
                for (const auto& [cell, mask] : layout.conveyor_cells)
                {
                    int rcx = 0, rcy = 0;
                    int rmask = 0;

                    if (rot == 0)
                    {
                        rcx = cell.first;
                        rcy = cell.second;
                        rmask = mask;
                    }
                    else if (rot == 90)
                    {
                        rcx = cell.second;
                        rcy = W_0 - 1 - cell.first;
                        if (mask & CONV_NORTH)
                            rmask |= CONV_EAST;
                        if (mask & CONV_SOUTH)
                            rmask |= CONV_WEST;
                        if (mask & CONV_EAST)
                            rmask |= CONV_SOUTH;
                        if (mask & CONV_WEST)
                            rmask |= CONV_NORTH;
                    }
                    else if (rot == 180)
                    {
                        rcx = W_0 - 1 - cell.first;
                        rcy = H_0 - 1 - cell.second;
                        if (mask & CONV_NORTH)
                            rmask |= CONV_SOUTH;
                        if (mask & CONV_SOUTH)
                            rmask |= CONV_NORTH;
                        if (mask & CONV_EAST)
                            rmask |= CONV_WEST;
                        if (mask & CONV_WEST)
                            rmask |= CONV_EAST;
                    }
                    else if (rot == 270)
                    {
                        rcx = H_0 - 1 - cell.second;
                        rcy = cell.first;
                        if (mask & CONV_NORTH)
                            rmask |= CONV_WEST;
                        if (mask & CONV_SOUTH)
                            rmask |= CONV_EAST;
                        if (mask & CONV_EAST)
                            rmask |= CONV_NORTH;
                        if (mask & CONV_WEST)
                            rmask |= CONV_SOUTH;
                    }

                    conveyor_cells[{bx + rcx, by + rcy}] |= rmask;
                    auto it_item = layout.conveyor_items.find(cell);
                    if (it_item != layout.conveyor_items.end())
                    {
                        conveyor_items[{bx + rcx, by + rcy}] = it_item->second;
                    }
                }
            }
        }

        if (active_count == 0)
        {
            std::cout << "\n\033[1;36m================================================="
                         "===============================\033[0m\n";
            std::cout << "\033[1;37m  GLOBAL 2D INDUSTRIAL COMPLEX: AREA \033[1;33m"
                      << _areas[a].name << " (" << W_a << "x" << H_a
                      << ")\033[0m - \033[1;30mNO FACTORIES ALLOCATED\033[0m\n";
            std::cout << "\033[1;36m==================================================="
                         "=============================\033[0m\n";
            continue;
        }

        // Stamp conveyors on grid
        for (const auto& [cell, mask] : conveyor_cells)
        {
            if (grid.count(cell))
                continue;

            CellInfo ci;
            ci.bg = -1;

            std::string item = "";
            auto it_item = conveyor_items.find(cell);
            if (it_item != conveyor_items.end())
            {
                item = it_item->second;
            }

            if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                (mask & CONV_EAST) && (mask & CONV_WEST))
            {
                ci.fg = 231; // Bright white for 4-way crossings
            }
            else if (!item.empty())
            {
                ci.fg = getItemColor(item);
            }
            else
            {
                ci.fg = 220;
            }

            std::string sym = "══";
            if (mask == (CONV_NORTH | CONV_SOUTH))
                sym = "║ ";
            else if (mask == (CONV_EAST | CONV_WEST))
                sym = "══";
            else if (mask == (CONV_NORTH | CONV_EAST))
                sym = "╚═";
            else if (mask == (CONV_SOUTH | CONV_EAST))
                sym = "╔═";
            else if (mask == (CONV_NORTH | CONV_WEST))
                sym = "╝ ";
            else if (mask == (CONV_SOUTH | CONV_WEST))
                sym = "╗ ";
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                     (mask & CONV_EAST) && (mask & CONV_WEST))
                sym = "╬═";
            else if ((mask & CONV_EAST) && (mask & CONV_WEST) &&
                     (mask & CONV_NORTH))
                sym = "╩═";
            else if ((mask & CONV_EAST) && (mask & CONV_WEST) &&
                     (mask & CONV_SOUTH))
                sym = "╦═";
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                     (mask & CONV_EAST))
                sym = "╠═";
            else if ((mask & CONV_NORTH) && (mask & CONV_SOUTH) &&
                     (mask & CONV_WEST))
                sym = "╣ ";
            else if (mask & (CONV_NORTH | CONV_SOUTH))
                sym = "║ ";
            else if (mask & (CONV_EAST | CONV_WEST))
                sym = "══";

            ci.text = sym;
            grid[cell] = ci;
        }

        // Print Global Area Map
        std::cout << "\n\033[1;36m================================================="
                     "===============================\033[0m\n";
        std::cout << "\033[1;37m  GLOBAL 2D INDUSTRIAL COMPLEX: AREA \033[1;33m"
                  << _areas[a].name << " (" << W_a << " x " << H_a
                  << ")\033[0m\n";
        std::cout << "  Factories Placed: \033[1;32m" << active_count
                  << "\033[0m\n";
        for (const auto& s : placed_summary)
        {
            std::cout << "   • " << s << "\n";
        }
        std::cout << "\033[1;36m==================================================="
                     "=============================\033[0m\n\n";

        // Top depot line banner
        bool has_top_depot = false;
        bool has_right_depot = false;
        for (const auto& d : _areas[a].static_depots)
        {
            if (d.y1 == H_a && d.y2 == H_a)
                has_top_depot = true;
            if (d.x1 == W_a && d.x2 == W_a)
                has_right_depot = true;
        }

        if (has_top_depot)
        {
            std::cout << "        \033[1;33m";
            for (int x = 0; x < W_a; ++x)
                std::cout << "══";
            std::cout << " [TOP STATIC DEPOT LINE]\033[0m\n";
        }

        // X-axis tens
        if (W_a >= 10)
        {
            std::cout << "        ";
            for (int x = 0; x < W_a; ++x)
            {
                int t = x / 10;
                if (t > 0)
                    std::cout << " " << t;
                else
                    std::cout << "  ";
            }
            std::cout << "\n";
        }
        // X-axis ones
        std::cout << "        ";
        for (int x = 0; x < W_a; ++x)
        {
            std::cout << " " << (x % 10);
        }
        std::cout << "\n";

        // Top border
        std::cout << "      ┌";
        for (int x = 0; x < W_a; ++x)
            std::cout << "──";
        std::cout << "┐\n";

        // Rows
        for (int y = H_a - 1; y >= 0; --y)
        {
            std::cout << "  " << std::setw(2) << y << "  │";
            for (int x = 0; x < W_a; ++x)
            {
                auto it = grid.find({x, y});
                if (it != grid.end())
                {
                    if (it->second.bg >= 0)
                    {
                        std::cout << "\033[48;5;" << it->second.bg << "m\033[38;5;"
                                  << it->second.fg << "m" << it->second.text
                                  << "\033[0m";
                    }
                    else
                    {
                        std::cout << "\033[38;5;" << it->second.fg << "m"
                                  << it->second.text << "\033[0m";
                    }
                }
                else
                {
                    std::cout << "\033[38;5;239m ·\033[0m";
                }
            }
            std::cout << "│  " << std::setw(2) << y;
            if (has_right_depot)
            {
                std::cout << " \033[1;33m║ [DEPOT]\033[0m";
            }
            std::cout << "\n";
        }

        // Bottom border
        std::cout << "      └";
        for (int x = 0; x < W_a; ++x)
            std::cout << "──";
        std::cout << "┘\n";

        // Bottom X-axis
        std::cout << "        ";
        for (int x = 0; x < W_a; ++x)
        {
            std::cout << " " << (x % 10);
        }
        std::cout << "\n\n";

        // Print Facility Legend for Area
        std::cout << "\033[1;36m---------------------------------------------------"
                     "-----------------------------\033[0m\n";
        std::cout << "\033[1;37m  AREA FACILITY LEGEND (Unified Colors)\033[0m\n";
        std::cout << "\033[1;36m---------------------------------------------------"
                     "-----------------------------\033[0m\n";
        std::cout << "  \033[48;5;208m  \033[0m [U ] Unloader (Depot Docking) | "
                  << "\033[48;5;39m  \033[0m [R ] Refining Unit | "
                  << "\033[48;5;48m  \033[0m [F ] Fitting Unit\n";
        std::cout << "  \033[48;5;177m  \033[0m [S ] Shredding Unit           | "
                  << "\033[48;5;33m  \033[0m [PK] Packaging Unit | "
                  << "\033[48;5;220m  \033[0m [PS] Protocol Stash\n";

        std::set<std::string> unique_area_items;
        for (const auto& [cell, item] : conveyor_items)
        {
            if (!item.empty())
                unique_area_items.insert(item);
        }
        if (!unique_area_items.empty())
        {
            std::cout << "  Conveyors: ";
            for (const auto& item : unique_area_items)
            {
                int color = getItemColor(item);
                std::cout << "\033[38;5;" << color << "m══ " << item
                          << "\033[0m   ";
            }
            std::cout << "\n";
        }
        std::cout << "  Turns: ╔═ ╗ ╚═ ╝ | Crossing: \033[38;5;231m╬═\033[0m\n";
        std::cout << "  Port Arrows: ▲ ▼ ► ◄ (Output = exit facility, Input = enter facility, colored by item)\n";
        std::cout << "\033[1;36m==================================================="
                     "=============================\033[0m\n\n";
    }
}
