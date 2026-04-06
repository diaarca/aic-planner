#pragma once

#include "area.hpp"
#include "fuel.hpp"
#include "mineral.hpp"
#include "ortools/linear_solver/linear_solver.h"
#include "product.hpp"
#include "region.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace operations_research;

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

    std::unique_ptr<MPSolver> _solver;
    std::vector<MPVariable*> _qty_produced;
    std::vector<std::vector<MPVariable*>> _factories_in_area;
    std::vector<MPVariable*> _num_batteries_active;
};
