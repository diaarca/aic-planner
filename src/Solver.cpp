#include "Solver.hpp"
#include <ilcplex/ilocplex.h>
#include <iostream>

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
    _env = IloEnv();
    try
    {
        _model = IloModel(_env);
        _qty_produced = IloNumVarArray(_env);
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
        IloNumVarArray(_env, _products.size(), 0, IloInfinity, ILOINT);

    for (size_t i = 0; i < _products.size(); ++i)
    {
        _qty_produced[i].setName((_products[i].name + "_prod").c_str());
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

    _num_batteries_active = IloIntVarArray(_env, _fuels.size(), 0, IloIntMax);
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        _num_batteries_active[i].setName((_fuels[i].name + "_active").c_str());
    }
}

void Solver::declareConstraints()
{
    // Mappings for easier lookup
    std::map<std::string, size_t> product_map;
    for (size_t i = 0; i < _products.size(); ++i)
    {
        product_map[_products[i].name] = i;
    }

    std::map<std::string, size_t> fuel_map;
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        fuel_map[_fuels[i].name] = i;
    }

    std::map<std::string, size_t> mineral_map;
    for (size_t i = 0; i < _mineral_limits.size(); ++i)
    {
        mineral_map[_mineral_limits[i].name] = i;
    }

    // Variables for fuel consumption (units per minute)
    IloNumExprArray fuel_consumption_per_min(_env, _fuels.size());
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        fuel_consumption_per_min[i] =
            _num_batteries_active[i] * (60.0 / _fuels[i].duration);
    }

    // Objective: Maximize total net value (sold products)
    IloExpr objective(_env);
    IloExpr total_net_production_rate(_env);
    for (size_t i = 0; i < _products.size(); ++i)
    {
        IloExpr qty_sold(_env);
        qty_sold += _qty_produced[i];
        if (fuel_map.count(_products[i].name))
        {
            size_t fuel_idx = fuel_map.at(_products[i].name);
            qty_sold -= fuel_consumption_per_min[fuel_idx];
        }
        objective += _products[i].value * qty_sold;
        total_net_production_rate += qty_sold;
        // Non-negative sold quantity
        _model.add(qty_sold >= 0);
        qty_sold.end();
    }
    _model.add(IloMaximize(_env, objective));

    // Storage constraint: total net production rate must not fill storage in
    // less than 48 hours
    if (_region.storage > 0)
    {
        _model.add(total_net_production_rate <= _region.storage / (24 * 60));
    }

    objective.end();
    total_net_production_rate.end();

    // Mineral limits (Ingredients + Direct Fuel Usage)
    for (size_t m = 0; m < _mineral_limits.size(); ++m)
    {
        const std::string& mineral_name = _mineral_limits[m].name;
        double mineral_limit = _mineral_limits[m].limit;

        IloExpr mineral_consumption_expr(_env);
        // From production
        for (size_t i = 0; i < _products.size(); ++i)
        {
            if (_products[i].mineral_consumption.count(mineral_name))
            {
                mineral_consumption_expr +=
                    _products[i].mineral_consumption.at(mineral_name) *
                    _qty_produced[i];
            }
        }
        // From direct fuel usage (if the mineral is a fuel)
        if (fuel_map.count(mineral_name))
        {
            size_t fuel_idx = fuel_map.at(mineral_name);
            mineral_consumption_expr += fuel_consumption_per_min[fuel_idx];
        }

        IloConstraint con = mineral_consumption_expr <= mineral_limit;
        con.setName(mineral_name.c_str());
        _model.add(con);
        mineral_consumption_expr.end();
    }

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
        IloExpr area_depot_1d_used(_env);
        IloExpr area_depot_2d_used(_env);
        for (size_t i = 0; i < _products.size(); ++i)
        {
            double factory_area =
                _products[i].factory_width * _products[i].factory_height;
            area_space_used += _factories_in_area[i][j] * factory_area;

            // 1D depot usage: sum of depot widths
            area_depot_1d_used +=
                _factories_in_area[i][j] * _products[i].factory_depot;

            // 2D depot usage: sum of depot areas
            // Assume the depot part of the factory is factory_depot *
            // factory_height
            double factory_depot_area =
                _products[i].factory_depot * _products[i].factory_height;
            area_depot_2d_used += _factories_in_area[i][j] * factory_depot_area;
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

        if (_areas[j].pac_depot_height > 0)
        {
            // 2D Depot Constraint
            double total_depot_area =
                _areas[j].pac_depot_width * _areas[j].pac_height;
            // Actually, if it's both in height and width, it might be
            // pac_depot_width * pac_depot_height
            if (_areas[j].pac_depot_height > 0)
                total_depot_area =
                    _areas[j].pac_depot_width * _areas[j].pac_depot_height;

            _model.add(area_depot_2d_used <= total_depot_area);
        }
        else if (_areas[j].pac_depot_width > 0)
        {
            // 1D Depot Constraint
            _model.add(area_depot_1d_used <= _areas[j].pac_depot_width);
        }

        area_space_used.end();
        area_depot_1d_used.end();
        area_depot_2d_used.end();
    }

    // Power consumption constraints
    IloExpr power_demand(_env);
    // 1. Ziplines and defenses of all areas
    for (const auto& area : _areas)
    {
        if (area.area_facilities.count("zipline") &&
            _facility_power.count("zipline"))
        {
            power_demand += area.area_facilities.at("zipline") *
                            _facility_power.at("zipline");
        }
        if (area.area_facilities.count("defense") &&
            _facility_power.count("defense"))
        {
            power_demand += area.area_facilities.at("defense") *
                            _facility_power.at("defense");
        }
    }

    // 2. Used facilities for the factories
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double factory_power = 0;
        for (const auto& f : _products[i].factory_facilities)
        {
            if (_facility_power.count(f.first))
            {
                factory_power += f.second * _facility_power.at(f.first);
            }
        }
        if (factory_power > 0)
        {
            for (size_t j = 0; j < _areas.size(); ++j)
            {
                power_demand += _factories_in_area[i][j] * factory_power;
            }
        }
    }

    // Power provided by active batteries
    IloExpr power_supply(_env);
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        power_supply += _num_batteries_active[i] * _fuels[i].power;
    }

    _model.add(power_supply + _region.base_power >= power_demand);

    power_demand.end();
    power_supply.end();
    fuel_consumption_per_min.end();
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

        if (produced > 1e-6)
        {
            double total_factories = 0;
            for (size_t j = 0; j < _areas.size(); ++j)
            {
                total_factories += _cplex.getValue(_factories_in_area[i][j]);
            }
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
            double used_depot_1d = 0;
            double used_depot_2d = 0;
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
                    used_depot_1d += num_f * _products[i].factory_depot;
                    used_depot_2d += num_f * (_products[i].factory_depot *
                                              _products[i].factory_height);
                }
            }
            double total_area = _areas[j].pac_width * _areas[j].pac_height;
            std::cout << "  Space used: " << used_space << " / " << total_area
                      << std::endl;
            if (_areas[j].pac_depot_height > 0)
            {
                double total_depot_area =
                    _areas[j].pac_depot_width * _areas[j].pac_depot_height;
                std::cout << "  Depot (2D) area used: " << used_depot_2d
                          << " / " << total_depot_area << std::endl;
            }
            else
            {
                std::cout << "  Depot (1D) length used: " << used_depot_1d
                          << " / " << _areas[j].pac_depot_width << std::endl;
            }
        }
    }

    std::cout << "\n--- Mineral Consumption (usage / limit) ---" << std::endl;
    for (const auto& mineral : _mineral_limits)
    {
        const std::string& mineral_name = mineral.name;
        double mineral_limit = mineral.limit;

        double total_consumed = 0.0;
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
    }

    std::cout << "\n--- Power Consumption ---" << std::endl;
    double power_ziplines = 0;
    double power_defenses = 0;
    for (const auto& area : _areas)
    {
        if (area.area_facilities.count("zipline") &&
            _facility_power.count("zipline"))
        {
            power_ziplines += area.area_facilities.at("zipline") *
                              _facility_power.at("zipline");
        }
        if (area.area_facilities.count("defense") &&
            _facility_power.count("defense"))
        {
            power_defenses += area.area_facilities.at("defense") *
                              _facility_power.at("defense");
        }
    }

    double power_factories = 0;
    for (size_t i = 0; i < _products.size(); ++i)
    {
        double factory_power = 0;
        for (const auto& f : _products[i].factory_facilities)
        {
            if (_facility_power.count(f.first))
            {
                factory_power += f.second * _facility_power.at(f.first);
            }
        }
        if (factory_power > 0)
        {
            for (size_t j = 0; j < _areas.size(); ++j)
            {
                power_factories +=
                    _cplex.getValue(_factories_in_area[i][j]) * factory_power;
            }
        }
    }

    std::cout << "Power for Ziplines: " << power_ziplines << std::endl;
    std::cout << "Power for Defenses: " << power_defenses << std::endl;
    std::cout << "Power for Factories: " << power_factories << std::endl;
    double total_needed = power_ziplines + power_defenses + power_factories;
    std::cout << "Total Power Needed: " << total_needed << std::endl;

    std::cout << "\n--- Power Production (Optimal Battery Mix) ---"
              << std::endl;
    double total_supply = 0;
    for (size_t i = 0; i < _fuels.size(); ++i)
    {
        double num_active = _cplex.getValue(_num_batteries_active[i]);
        if (num_active > 0.5)
        {
            double supply = num_active * _fuels[i].power;
            double consumption = num_active * (60.0 / _fuels[i].duration);
            std::cout << _fuels[i].name << ": " << (int)(num_active + 0.5)
                      << " active batteries (" << supply << " power, "
                      << consumption << " units/min consumption)" << std::endl;
            total_supply += supply;
        }
    }
    std::cout << "Total Power Provided: " << total_supply << std::endl;
}
