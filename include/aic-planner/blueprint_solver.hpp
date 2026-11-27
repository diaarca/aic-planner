#pragma once

#include "area.hpp"
#include "blueprint_layout.hpp"
#include "mineral.hpp"
#include "product.hpp"
#include "recipe_graph.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aic
{
struct FactoryNetwork;

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
    double compact_seconds = 0.0;
    bool allow_overpasses = true;
    std::optional<std::pair<int, int>> blueprint_size = std::nullopt;
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
    const Facility *electric_pylon_ = nullptr;
    struct RoutedLayout
    {
        BlueprintLayout layout;
        std::vector<int> entity_order, stream_order;
    };
    std::map<std::string, RoutedLayout> routed_layouts_;

    struct PlacementProblem;
    bool solve_placement(const PlacementProblem &problem, FactoryNetwork network,
                         const std::map<int, std::vector<int>> &stages, BlueprintLayout &layout);
    bool is_mineral(const std::string &item) const;
};

} // namespace aic
