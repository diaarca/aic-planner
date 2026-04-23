#include "solver.hpp"
#include <iostream>

using namespace operations_research;

Solver::Solver(const std::vector<Product>& products,
               const std::vector<Mineral>& mineral_limits,
               const std::vector<Area>& areas,
               const std::vector<Fuel>& fuels,
               const std::map<std::string, double>& facility_power,
               const Region& region)
    : _products(products), _mineral_limits(mineral_limits), _areas(areas),
      _fuels(fuels), _facility_power(facility_power), _region(region)
{
}

void Solver::solve()
{
    _solver = std::unique_ptr<MPSolver>(MPSolver::CreateSolver("SCIP"));
    if (!_solver)
    {
        std::cerr << "SCIP solver not available." << std::endl;
        return;
    }

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
    const double infinity = _solver->infinity();

    _qty_produced.clear();
    for (size_t i = 0; i < _products.size(); ++i)
    {
        _qty_produced.push_back(
            _solver->MakeIntVar(0, infinity, _products[i].name + "_prod"));
    }

    _factories_in_area.clear();
    _factories_in_area.resize(_products.size());
    for (size_t i = 0; i < _products.size(); ++i)
    {
        for (size_t j = 0; j < _areas.size(); ++j)
        {
            std::string name = _products[i].name + "_" + _areas[j].name;
            _factories_in_area[i].push_back(
                _solver->MakeIntVar(0, infinity, name));
        }
    }

    _num_batteries_active.clear();
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        _num_batteries_active.push_back(
            _solver->MakeIntVar(0, infinity, _fuels[i].name + "_active"));
    }
}

void Solver::declareConstraints()
{
    const double infinity = _solver->infinity();

    // Mappings for easier lookup
    std::map<std::string, size_t> fuel_map;
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        fuel_map[_fuels[i].name] = i;
    }

    // Objective: Maximize total net value (sold products)
    MPObjective* const objective = _solver->MutableObjective();
    objective->SetMaximization();

    for (size_t i = 0; i < _products.size(); ++i)
    {
        objective->SetCoefficient(_qty_produced[i], _products[i].value);

        if (fuel_map.count(_products[i].name))
        {
            size_t fuel_idx = fuel_map.at(_products[i].name);
            double fuel_consumption_per_min_coeff =
                (60.0 / _fuels[fuel_idx].duration);

            objective->SetCoefficient(
                _num_batteries_active[fuel_idx],
                objective->GetCoefficient(_num_batteries_active[fuel_idx]) -
                    _products[i].value * fuel_consumption_per_min_coeff);

            MPConstraint* const c_fuel_cons =
                _solver->MakeRowConstraint(0, infinity);

            c_fuel_cons->SetCoefficient(_qty_produced[i], 1.0);
            c_fuel_cons->SetCoefficient(_num_batteries_active[fuel_idx],
                                        -fuel_consumption_per_min_coeff);
        }
    }

    // the product storage must not be full within 48 hours
    for (size_t i = 0; i < _products.size(); ++i)
    {
        MPConstraint* const c_storage = _solver->MakeRowConstraint(
            -infinity, _region.storage / (48.0 * 60.0));

        c_storage->SetCoefficient(_qty_produced[i], 1.0);
        if (fuel_map.count(_products[i].name))
        {
            size_t fuel_idx = fuel_map.at(_products[i].name);
            double fuel_consumption_per_min_coeff =
                (60.0 / _fuels[fuel_idx].duration);
            c_storage->SetCoefficient(
                _num_batteries_active[fuel_idx],
                c_storage->GetCoefficient(_num_batteries_active[fuel_idx]) -
                    fuel_consumption_per_min_coeff);
        }
    }

    // Mineral limits
    for (size_t m = 0; m < _mineral_limits.size(); ++m)
    {
        const std::string& mineral_name = _mineral_limits[m].name;
        double mineral_limit = _mineral_limits[m].limit;

        MPConstraint* const c_mineral =
            _solver->MakeRowConstraint(-infinity, mineral_limit, mineral_name);

        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_products[i].mineral_consumption.count(mineral_name))
            {
                c_mineral->SetCoefficient(
                    _qty_produced[i],
                    _products[i].mineral_consumption.at(mineral_name));
            }
        }
    }

    // Factory capacity
    for (size_t i = 0; i < _products.size(); ++i)
    {
        MPConstraint* const c_nb_factory =
            _solver->MakeRowConstraint(-infinity, 0);
        c_nb_factory->SetCoefficient(_qty_produced[i], 1.0);
        for (size_t j = 0; j < _areas.size(); ++j)
        {
            double units_per_minute = 60.0 / _products[i].production_time;
            c_nb_factory->SetCoefficient(_factories_in_area[i][j],
                                         -units_per_minute);
        }
    }

    // Area space and depot constraints
    for (size_t j = 0; j < _areas.size(); ++j)
    {
        double total_available_area =
            _areas[j].pac_width * _areas[j].pac_height;
        if (total_available_area > 0)
        {
            // Total space
            MPConstraint* const c_space =
                _solver->MakeRowConstraint(-infinity, total_available_area);
            for (size_t i = 0; i < _products.size(); ++i)
            {
                double factory_area =
                    _products[i].factory_width * _products[i].factory_height;
                c_space->SetCoefficient(_factories_in_area[i][j], factory_area);
            }

            // Depot constraint
            double total_depot_in_area =
                _areas[j].pac_depot_width + _areas[j].pac_depot_height;
            MPConstraint* const c_depot =
                _solver->MakeRowConstraint(-infinity, total_depot_in_area);
            for (size_t i = 0; i < _products.size(); ++i)
            {
                double factory_depot = _products[i].factory_depot;
                c_depot->SetCoefficient(_factories_in_area[i][j],
                                        factory_depot);
            }
        }
        else
        {
            MPConstraint* const c_space = _solver->MakeRowConstraint(0, 0);

            for (size_t i = 0; i < _products.size(); ++i)
            {
                double factory_area =
                    _products[i].factory_width * _products[i].factory_height;
                c_space->SetCoefficient(_factories_in_area[i][j], factory_area);
            }
        }
    }

    // Power
    MPConstraint* const c_power_con =
        _solver->MakeRowConstraint(-infinity, _region.base_power);
    for (const auto& area : _areas)
    {
        double static_demand = 0;
        if (area.area_facilities.count("zipline") &&
            _facility_power.count("zipline"))
            static_demand += area.area_facilities.at("zipline") *
                             _facility_power.at("zipline");
        if (area.area_facilities.count("defense") &&
            _facility_power.count("defense"))
            static_demand += area.area_facilities.at("defense") *
                             _facility_power.at("defense");
        if (area.area_facilities.count("mining_rig") &&
            _facility_power.count("mining_rig"))
            static_demand += area.area_facilities.at("mining_rig") *
                             _facility_power.at("mining_rig");
        if (static_demand > 0)
            c_power_con->SetBounds(c_power_con->lb(),
                                   c_power_con->ub() - static_demand);
    }
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double factory_power = 0;
        for (const auto& f : _products[i].factory_facilities)
        {
            if (_facility_power.count(f.first))
                factory_power += f.second * _facility_power.at(f.first);
        }
        if (factory_power > 0)
        {
            for (size_t j = 0; j < _areas.size(); ++j)
                c_power_con->SetCoefficient(_factories_in_area[i][j],
                                            factory_power);
        }
    }
    for (size_t i = 0; i < _fuels.size(); ++i)
        c_power_con->SetCoefficient(_num_batteries_active[i], -_fuels[i].power);
}

bool Solver::solveModel()
{
    const MPSolver::ResultStatus result_status = _solver->Solve();
    return result_status == MPSolver::OPTIMAL;
}

void Solver::displaySolution()
{
    std::cout << "Solution Status: OPTIMAL" << std::endl;
    std::cout << "Optimal Objective Value (Net Value per Minute): "
              << _solver->Objective().Value() << std::endl;

    std::cout << "\n--- Production Plan (units per minute) ---" << std::endl;
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double produced = _qty_produced[i]->solution_value();
        if (produced > 1e-6)
        {
            double total_factories = 0;
            for (size_t j = 0; j < _areas.size(); ++j)
                total_factories += _factories_in_area[i][j]->solution_value();
            std::cout << _products[i].name << ": " << produced << " units ["
                      << total_factories << " factories]" << std::endl;
        }
    }

    std::cout << "\n--- Factory Placement ---" << std::endl;
    for (size_t j = 0; j < _areas.size(); ++j)
    {
        bool area_used = false;
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_factories_in_area[i][j]->solution_value() > 0.5)
            {
                area_used = true;
                break;
            }
        }
        if (area_used)
        {
            std::cout << "Area: " << _areas[j].name << std::endl;
            double used_space = 0;
            double used_depot = 0;
            for (size_t i = 0; i < _products.size(); ++i)
            {
                double num_f = _factories_in_area[i][j]->solution_value();
                if (num_f > 0.5)
                {
                    std::cout << "  - " << _products[i].name << ": "
                              << (int)(num_f + 0.5) << " factories"
                              << std::endl;
                    used_space += num_f * (_products[i].factory_width *
                                           _products[i].factory_height);
                    used_depot += num_f * _products[i].factory_depot;
                }
            }
            std::cout << "  Space used: " << used_space << " / "
                      << (_areas[j].pac_width * _areas[j].pac_height)
                      << std::endl;

            std::cout << "  Depot length used: " << used_depot << " / "
                      << _areas[j].pac_depot_width + _areas[j].pac_depot_height
                      << std::endl;
        }
    }

    std::cout << "\n--- Mineral Consumption (usage / limit) ---" << std::endl;
    for (const auto& mineral : _mineral_limits)
    {
        double total_consumed = 0.0;
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_products[i].mineral_consumption.count(mineral.name))
                total_consumed +=
                    _products[i].mineral_consumption.at(mineral.name) *
                    _qty_produced[i]->solution_value();
        }
        for (size_t i = 0; i < _fuels.size(); ++i)
        {
            if (_fuels[i].name == mineral.name)
                total_consumed += _num_batteries_active[i]->solution_value() *
                                  (60.0 / _fuels[i].duration);
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
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double f_pow = 0;
        for (const auto& f : _products[i].factory_facilities)
            if (_facility_power.count(f.first))
                f_pow += f.second * _facility_power.at(f.first);
        for (size_t j = 0; j < _areas.size(); ++j)
            p_fact += _factories_in_area[i][j]->solution_value() * f_pow;
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
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        double num = _num_batteries_active[i]->solution_value();
        if (num > 0.5)
        {
            double s = num * _fuels[i].power;
            std::cout << _fuels[i].name << ": " << (int)(num + 0.5)
                      << " active batteries (" << s << " power, "
                      << num * (60.0 / _fuels[i].duration)
                      << " units/min consumption)" << std::endl;
            t_supp += s;
        }
    }
    std::cout << "Total Power Provided: " << t_supp << std::endl;
}
