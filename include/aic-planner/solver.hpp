#pragma once

#include "area.hpp"
#include "fuel.hpp"
#include "mineral.hpp"
#include "ortools/sat/cp_model.h"
#include "product.hpp"
#include "region.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "blueprint_solver.hpp"

#define NB_HOUR_BEFORE_OVERFLOW 48

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
    void setBlueprintLayouts(const std::map<std::string, BlueprintLayout>& layouts);

  private:
    void instantiateVariables();
    void declareConstraints();
    bool solveModel();
    void displaySolution();
    void displayAreaLayouts();

    const std::vector<Product>& _products;
    const std::vector<Mineral>& _mineral_limits;
    const std::vector<Area>& _areas;
    const std::vector<Fuel>& _fuels;
    const std::map<std::string, double>& _facility_power;
    const Region& _region;

    operations_research::sat::CpModelBuilder _cp_model;
    operations_research::sat::CpSolverResponse _response;
    std::vector<operations_research::sat::IntVar> _qty_produced;
    std::vector<std::vector<operations_research::sat::IntVar>>
        _factories_in_area;
    std::vector<operations_research::sat::IntVar> _num_batteries_active;
    int64_t _obj_scale_factor;

    struct AreaSlotVar
    {
        operations_research::sat::BoolVar active;
        std::vector<operations_research::sat::BoolVar> is_prod;
        std::map<int, operations_research::sat::BoolVar> is_rot;
        operations_research::sat::IntVar bx;
        operations_research::sat::IntVar by;
        operations_research::sat::IntVar bw;
        operations_research::sat::IntVar bh;
    };
    std::map<std::string, BlueprintLayout> _product_layouts;
    std::vector<std::vector<AreaSlotVar>> _area_slots;
};
