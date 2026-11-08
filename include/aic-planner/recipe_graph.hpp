#pragma once
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
    std::vector<std::string> startup_items;
    std::map<std::string, int> startup_stock;
};

// Resolve steady-state material balances, including productive recycling loops.
class RecipeGraph
{
  public:
    RecipeGraph(const std::vector<Facility> &facilities, const std::vector<Mineral> &resources,
                bool automatic = false);
    const Recipe *find_recipe(const std::string &item, const Facility *&facility) const;
    const Facility &facility(const std::string &name) const;
    bool is_resource(const std::string &item) const;
    std::vector<SeedLoopSpec> seed_loops() const;
    ProductionPlan plan_target(const std::string &target) const;
    void validate_target(const std::string &item) const;

  private:
    std::map<std::string, std::vector<std::pair<const Facility *, const Recipe *>>> recipes_;
    std::set<std::string> resources_;
    std::map<std::string, const Facility *> facilities_;
    bool automatic_ = false;
    mutable std::map<std::string, std::pair<const Facility *, const Recipe *>> selected_;
};
} // namespace aic
