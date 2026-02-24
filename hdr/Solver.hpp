#pragma once

#include "Product.hpp"
#include "Area.hpp"
#include <ilcplex/ilocplex.h>
#include <map>
#include <string>
#include <vector>

class Solver
{
  public:
    Solver(const std::vector<Product>& products,
           const std::map<std::string, double>& mineral_limits,
           const std::vector<Area>& areas,
           const std::map<std::string, double>& facilities_power,
           const std::map<std::string, std::pair<double, double>>& mineral_fuels);
    void solve();

  private:
    void instantiateVariables();
    void declareConstraints();
    bool solveModel();
    void displaySolution();

    const std::vector<Product>& _products;
    const std::map<std::string, double>& _mineral_limits;
    const std::vector<Area>& _areas;
    const std::map<std::string, double>& _facilities_power;
    const std::map<std::string, std::pair<double, double>>& _mineral_fuels;

    IloEnv _env;
    IloModel _model;
    IloNumVarArray _qty_produced;
    IloNumVarArray _qty_fuel_product; // Fuel consumption of products
    IloNumVarArray _qty_fuel_mineral; // Fuel consumption of minerals (like originium)
    IloArray<IloNumVarArray> _factories_in_area; // factories_in_area[product_idx][area_idx]
    IloCplex _cplex;
};
