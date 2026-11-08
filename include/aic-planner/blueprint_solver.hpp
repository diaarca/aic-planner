#pragma once

#include "area.hpp"
#include "blueprint_layout.hpp"
#include "mineral.hpp"
#include "product.hpp"
#include "recipe_graph.hpp"
#include <map>
#include <string>
#include <vector>

namespace aic
{

enum class BlueprintStatus
{
    Success,
    Infeasible,
    Timeout,
    RoutingExhausted
};

struct BlueprintOptions
{
    double placement_seconds = 2.0;
    int max_attempts = 50;
    bool automatic_recipes = false;
};

class BlueprintSolver
{
  public:
    BlueprintSolver(const std::vector<Facility> &facilities, const std::vector<Mineral> &minerals,
                    const std::vector<Area> &areas, BlueprintOptions options = {});

    bool solve(Product &product, BlueprintLayout &layout);
    BlueprintStatus status() const
    {
        return status_;
    }

  private:
    RecipeGraph graph_;
    BlueprintOptions options_;
    BlueprintStatus status_ = BlueprintStatus::Infeasible;
    const std::vector<Area> &areas_;

    bool is_mineral(const std::string &item) const;
};

} // namespace aic
