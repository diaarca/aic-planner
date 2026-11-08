#pragma once

#include "area.hpp"
#include "fuel.hpp"
#include "mineral.hpp"
#include "ortools/sat/cp_model.h"
#include "product.hpp"
#include "region.hpp"
#include <map>
#include <string>
#include <vector>

#include "blueprint_layout.hpp"

namespace aic
{

inline constexpr int inventory_horizon_hours = 48;

class Solver
{
  public:
    Solver(const std::vector<Product> &products, const std::vector<Mineral> &mineral_limits,
           const std::vector<Area> &areas, const std::vector<Fuel> &fuels,
           const std::map<std::string, double> &facility_power, const Region &region);
    bool solve();
    void set_time_limit(double seconds)
    {
        time_limit_seconds_ = seconds;
    }
    void set_render_layouts(bool enabled)
    {
        render_layouts_ = enabled;
    }
    double objective_value() const;
    std::string status() const;
    void set_blueprint_layouts(const std::map<std::string, BlueprintLayout> &layouts);

  private:
    bool render_layouts_ = true;
    double time_limit_seconds_ = 30.0;
    void instantiate_variables();
    void instantiate_area_slots();
    void declare_constraints();
    void declare_production_constraints();
    void declare_area_constraints();
    void declare_power_constraints();
    bool solve_model();
    void display_solution();
    void display_area_layouts();

    const std::vector<Product> &products_;
    const std::vector<Mineral> &mineral_limits_;
    const std::vector<Area> &areas_;
    const std::vector<Fuel> &fuels_;
    const std::map<std::string, double> &facility_power_;
    const Region &region_;

    operations_research::sat::CpModelBuilder cp_model_;
    operations_research::sat::CpSolverResponse response_;
    std::vector<operations_research::sat::IntVar> qty_produced_;
    std::vector<std::vector<operations_research::sat::IntVar>> factories_in_area_;
    std::vector<operations_research::sat::IntVar> num_batteries_active_;
    int64_t obj_scale_factor_;

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
    std::map<std::string, BlueprintLayout> product_layouts_;
    std::vector<std::vector<AreaSlotVar>> area_slots_;
};

} // namespace aic
