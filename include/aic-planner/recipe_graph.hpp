#pragma once
#include "area.hpp"
#include "facility.hpp"
#include "mineral.hpp"
#include <map>
#include <set>

namespace aic
{
enum class SeedLoopRole
{
    None,
    Picker,
    Planter,
    Harvest,
    Pylon
};
struct SeedLoopSpec
{
    std::string seed, flower;
    const Recipe *picker = nullptr;
    const Recipe *planter = nullptr;
    bool hydroponic = false;
};
struct ProductionStep
{
    const Facility *facility = nullptr;
    const Recipe *recipe = nullptr;
    double cycles_per_second = 0;
    int instances = 0;
    std::string seed_loop;
    SeedLoopRole loop_role = SeedLoopRole::None;
};
struct ProductionPlan
{
    double target_rate = 0;
    std::vector<ProductionStep> steps;
    std::map<std::string, double> external_rates;
    std::map<std::string, MaterialPhase> phases;
    const Area *area = nullptr;
    std::map<std::string, const Facility *> source_facilities;
    const Facility *disperser = nullptr;
    const Facility *projector = nullptr;
    const Facility *pipe_splitter = nullptr;
    const Facility *pipe_converger = nullptr;
    const Facility *pipe_valve = nullptr;
    std::map<std::string, std::pair<const Facility *, double>> disposal;
    const Recipe *final_recipe = nullptr;
    std::vector<std::string> startup_items;
    std::map<std::string, int> startup_stock;
};

std::string array_mode(const Recipe &recipe);

// Resolve steady-state material balances, including productive recycling loops.
class RecipeGraph
{
  public:
    RecipeGraph(const std::vector<Facility> &facilities, const std::vector<Mineral> &resources,
                bool automatic = false, const Area *area = nullptr);
    const Recipe *find_recipe(const std::string &item, const Facility *&facility) const;
    const Facility &facility(const std::string &name) const;
    bool is_resource(const std::string &item) const;
    std::vector<SeedLoopSpec> seed_loops() const;
    ProductionPlan plan_target(const std::string &target,
                               const std::set<const Recipe *> &excluded_final = {}) const;
    void validate_target(const std::string &item) const;

  private:
    std::map<std::string, std::vector<std::pair<const Facility *, const Recipe *>>> recipes_;
    std::set<std::string> resources_;
    std::map<std::string, double> local_limits_;
    const Area *area_;
    std::map<std::string, const Facility *> facilities_;
    bool automatic_ = false;
    mutable std::map<std::string, std::pair<const Facility *, const Recipe *>> selected_;
};
} // namespace aic
