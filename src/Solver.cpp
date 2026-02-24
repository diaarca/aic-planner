#include "Solver.hpp"
#include <ilcplex/ilocplex.h>
#include <iostream>

Solver::Solver(const std::vector<Product>& products,
               const std::map<std::string, double>& mineral_limits,
               const std::vector<Area>& areas,
               const std::map<std::string, double>& facilities_power,
               const std::map<std::string, std::pair<double, double>>& mineral_fuels)
    : _products(products), _mineral_limits(mineral_limits), _areas(areas),
      _facilities_power(facilities_power), _mineral_fuels(mineral_fuels)
{
}

void Solver::solve()
{
    _env = IloEnv();
    try
    {
        _model = IloModel(_env);
        _qty_produced = IloNumVarArray(_env);
        _qty_fuel_product = IloNumVarArray(_env);
        _qty_fuel_mineral = IloNumVarArray(_env);
        _factories_in_area = IloArray<IloNumVarArray>(_env);

        instantiateVariables();
        declareConstraints();

        _cplex = IloCplex(_model);
        if (solveModel())
        {
            displaySolution();
        }
        else
        {
            std::cerr << "No solution found." << std::endl;
        }
    }
    catch (const IloException& e)
    {
        std::cerr << "\nConcert exception caught: " << e << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nStandard exception caught: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "\nUnknown exception caught" << std::endl;
    }

    _env.end();
}

void Solver::instantiateVariables()
{
    _qty_produced =
        IloNumVarArray(_env, _products.size(), 0, IloInfinity, ILOFLOAT);
    _qty_fuel_product =
        IloNumVarArray(_env, _products.size(), 0, IloInfinity, ILOFLOAT);
    
    for (size_t i = 0; i < _products.size(); ++i)
    {
        _qty_produced[i].setName((_products[i].name + "_prod").c_str());
        _qty_fuel_product[i].setName((_products[i].name + "_fuel").c_str());
    }

    _qty_fuel_mineral = IloNumVarArray(_env, _mineral_limits.size(), 0, IloInfinity, ILOFLOAT);
    size_t m_idx = 0;
    for (const auto& pair : _mineral_limits) {
        _qty_fuel_mineral[m_idx].setName((pair.first + "_fuel").c_str());
        m_idx++;
    }

    _factories_in_area = IloArray<IloNumVarArray>(_env, _products.size());
    for (size_t i = 0; i < _products.size(); ++i)
    {
        _factories_in_area[i] =
            IloNumVarArray(_env, _areas.size(), 0, IloInfinity, ILOINT);
        for (size_t j = 0; j < _areas.size(); ++j)
        {
            std::string name = _products[i].name + "_" + _areas[j].name;
            _factories_in_area[i][j].setName(name.c_str());
        }
    }
}

void Solver::declareConstraints()
{
    // Objective: Maximize total value of net production
    IloExpr objective(_env);
    for (size_t i = 0; i < _products.size(); ++i)
    {
        objective += _products[i].value * (_qty_produced[i] - _qty_fuel_product[i]);
    }
    _model.add(IloMaximize(_env, objective));
    objective.end();

    // Net production must be non-negative
    for (size_t i = 0; i < _products.size(); ++i) {
        _model.add(_qty_produced[i] >= _qty_fuel_product[i]);
    }

    // Mineral limits (Ingredients + Fuel)
    size_t m_idx = 0;
    for (const auto& pair : _mineral_limits)
    {
        const std::string& mineral_name = pair.first;
        double mineral_limit = pair.second;

        IloExpr mineral_consumption_expr(_env);
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_products[i].mineral_consumption.count(mineral_name))
            {
                mineral_consumption_expr +=
                    _products[i].mineral_consumption.at(mineral_name) *
                    _qty_produced[i];
            }
        }
        
        // Add mineral used as fuel
        mineral_consumption_expr += _qty_fuel_mineral[m_idx];

        IloConstraint con = mineral_consumption_expr <= mineral_limit;
        con.setName(mineral_name.c_str());
        _model.add(con);
        mineral_consumption_expr.end();
        m_idx++;
    }

    // Power Constraints
    IloExpr total_power_needed(_env);
    // Area facilities power
    for (size_t j = 0; j < _areas.size(); ++j) {
        for (const auto& pair : _areas[j].area_facilities) {
            const std::string& fac_name = pair.first;
            double count = pair.second;
            if (_facilities_power.count(fac_name)) {
                total_power_needed += count * _facilities_power.at(fac_name);
            }
        }
    }
    // Factory facilities power
    for (size_t i = 0; i < _products.size(); ++i) {
        double factory_power = 0;
        for (const auto& pair : _products[i].factory_facilities) {
            const std::string& fac_name = pair.first;
            double count = pair.second;
            if (_facilities_power.count(fac_name)) {
                factory_power += count * _facilities_power.at(fac_name);
            }
        }
        for (size_t j = 0; j < _areas.size(); ++j) {
            total_power_needed += _factories_in_area[i][j] * factory_power;
        }
    }

    IloExpr total_energy_provided(_env);
    // Product fuels
    for (size_t i = 0; i < _products.size(); ++i) {
        if (_products[i].is_fuel) {
            total_energy_provided += _qty_fuel_product[i] * _products[i].fuel_power * _products[i].fuel_duration;
        } else {
            // Cannot use non-fuel product as fuel
            _model.add(_qty_fuel_product[i] == 0);
        }
    }
    // Mineral fuels
    m_idx = 0;
    for (const auto& pair : _mineral_limits) {
        const std::string& mineral_name = pair.first;
        if (_mineral_fuels.count(mineral_name)) {
            double power = _mineral_fuels.at(mineral_name).first;
            double duration = _mineral_fuels.at(mineral_name).second;
            total_energy_provided += _qty_fuel_mineral[m_idx] * power * duration;
        } else {
            _model.add(_qty_fuel_mineral[m_idx] == 0);
        }
        m_idx++;
    }

    _model.add(total_energy_provided >= total_power_needed);
    total_power_needed.end();
    total_energy_provided.end();

    // Factory capacity constraints
    for (size_t i = 0; i < _products.size(); ++i)
    {
        IloExpr total_factory_capacity(_env);
        for (size_t j = 0; j < _areas.size(); ++j)
        {
            double units_per_minute = 60.0 / _products[i].production_time;
            total_factory_capacity +=
                _factories_in_area[i][j] * units_per_minute;
        }
        _model.add(_qty_produced[i] <= total_factory_capacity);
        total_factory_capacity.end();
    }

    // Area space and depot constraints
    for (size_t j = 0; j < _areas.size(); ++j)
    {
        IloExpr area_space_used(_env);
        IloExpr area_depot_used(_env);
        for (size_t i = 0; i < _products.size(); ++i)
        {
            double factory_area =
                _products[i].factory_width * _products[i].factory_height;
            area_space_used += _factories_in_area[i][j] * factory_area;
            area_depot_used +=
                _factories_in_area[i][j] * _products[i].factory_depot;
        }

        double total_available_area =
            _areas[j].pac_width * _areas[j].pac_height;
        if (total_available_area > 0)
        {
            _model.add(area_space_used <= total_available_area);
        }
        else
        {
            for (size_t i = 0; i < _products.size(); ++i)
            {
                _model.add(_factories_in_area[i][j] == 0);
            }
        }

        if (_areas[j].pac_depot > 0)
        {
            _model.add(area_depot_used <= _areas[j].pac_depot);
        }
        else
        {
            _model.add(area_depot_used == 0);
        }

        area_space_used.end();
        area_depot_used.end();
    }
}

bool Solver::solveModel()
{
    _cplex.setOut(_env.getNullStream());
    return _cplex.solve();
}

void Solver::displaySolution()
{
    std::cout << "Solution Status: " << _cplex.getStatus() << std::endl;
    std::cout << "Optimal Objective Value (Net Value per Minute): "
              << _cplex.getObjValue() << std::endl;

    std::cout << "\n--- Production Plan (units per minute) ---" << std::endl;
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double produced = _cplex.getValue(_qty_produced[i]);
        double used_as_fuel = _cplex.getValue(_qty_fuel_product[i]);
        double net = produced - used_as_fuel;
        
        if (produced > 1e-6)
        {
            double total_factories = 0;
            for (size_t j = 0; j < _areas.size(); ++j)
            {
                total_factories += _cplex.getValue(_factories_in_area[i][j]);
            }
            std::cout << _products[i].name << ": " << net << " net (produced: " << produced;
            if (used_as_fuel > 1e-6) std::cout << ", fuel: " << used_as_fuel;
            std::cout << ") [" << total_factories << " factories]" << std::endl;
        }
    }

    size_t m_idx = 0;
    for (const auto& pair : _mineral_limits) {
        double fuel_usage = _cplex.getValue(_qty_fuel_mineral[m_idx]);
        if (fuel_usage > 1e-6) {
            std::cout << "Mineral " << pair.first << " used as fuel: " << fuel_usage << std::endl;
        }
        m_idx++;
    }

    std::cout << "\n--- Factory Placement ---" << std::endl;
    for (size_t j = 0; j < _areas.size(); ++j)
    {
        bool area_used = false;
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_cplex.getValue(_factories_in_area[i][j]) > 0.5)
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
                double num_f = _cplex.getValue(_factories_in_area[i][j]);
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
            double total_area = _areas[j].pac_width * _areas[j].pac_height;
            std::cout << "  Space used: " << used_space << " / " << total_area
                      << std::endl;
            std::cout << "  Depot used: " << used_depot << " / "
                      << _areas[j].pac_depot << std::endl;
        }
    }

    std::cout << "\n--- Mineral Consumption (usage / limit) ---" << std::endl;
    m_idx = 0;
    for (const auto& pair : _mineral_limits)
    {
        const std::string& mineral_name = pair.first;
        double mineral_limit = pair.second;

        double total_consumed = _cplex.getValue(_qty_fuel_mineral[m_idx]);
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_products[i].mineral_consumption.count(mineral_name))
            {
                total_consumed +=
                    _products[i].mineral_consumption.at(mineral_name) *
                    _cplex.getValue(_qty_produced[i]);
            }
        }
        std::cout << mineral_name << ": " << total_consumed << " / "
                  << mineral_limit << std::endl;
        m_idx++;
    }
}
