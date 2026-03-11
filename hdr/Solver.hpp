#pragma once

#include "Area.hpp"
#include "Fuel.hpp"
#include "Mineral.hpp"
#include "Product.hpp"
#include "Region.hpp"
#include <ilcplex/ilocplex.h>
#include <string>
#include <vector>

class Solver
{
  public:
    Solver(const std::vector<Product>& products,
           const std::vector<Mineral>& mineral_limits,
           const std::vector<Area>& areas,
           const std::vector<Fuel>& fuels,
           const std::map<std::string, double>& facility_power,
           const Region& region);
    void solve();

  private:
    void instantiateVariables();
    void declareConstraints();
    bool solveModel();
    void displaySolution();

    const std::vector<Product>& _products;
    const std::vector<Mineral>& _mineral_limits;
    const std::vector<Area>& _areas;
    const std::vector<Fuel>& _fuels;
    const std::map<std::string, double>& _facility_power;
    const Region& _region;

    IloEnv _env;
    IloModel _model;
    IloNumVarArray _qty_produced;
    IloArray<IloNumVarArray> _factories_in_area;
    IloIntVarArray _num_batteries_active;
    IloCplex _cplex;
};
