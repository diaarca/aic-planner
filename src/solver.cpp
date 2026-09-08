#include "solver.hpp"
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
}

void Solver::declareConstraints()
{
    // Instanciate fuel map for easier lookup
    std::map<std::string, size_t> fuel_map;
    for (size_t fu = 0; fu < _fuels.size(); ++fu)
    {
        fuel_map[_fuels[fu].name] = fu;
    }

    // Objective: Maximize total net value (sold products)
    LinearExpr objective;

    for (size_t p = 0; p < _products.size(); ++p)
    {
        objective += LinearExpr::Term(
            _qty_produced[p],
            _obj_scale_factor * static_cast<int64_t>(_products[p].value));

        if (fuel_map.count(_products[p].name))
        {
            size_t fu = fuel_map.at(_products[p].name);
            int64_t d_fu = static_cast<int64_t>(_fuels[fu].duration);
            int64_t cost_coeff_fu = (_obj_scale_factor * 60 / d_fu) *
                                    static_cast<int64_t>(_products[p].value);
            objective +=
                LinearExpr::Term(_num_batteries_active[fu], -cost_coeff_fu);

            // Fuel Balance Constraint
            LinearExpr c_fuel_bal;
            c_fuel_bal += LinearExpr::Term(_qty_produced[p], d_fu);
            c_fuel_bal += LinearExpr::Term(_num_batteries_active[fu], -60);
            _cp_model.AddGreaterOrEqual(c_fuel_bal, 0);
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
}
