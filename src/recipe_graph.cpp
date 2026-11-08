#include "recipe_graph.hpp"
#include "logging.hpp"
#include "ortools/linear_solver/linear_solver.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace aic
{
RecipeGraph::RecipeGraph(const std::vector<Facility> &facilities,
                         const std::vector<Mineral> &resources, bool automatic)
    : automatic_(automatic)
{
    for (const auto &resource : resources)
        resources_.insert(resource.name);
    for (const auto &facility : facilities)
    {
        facilities_[facility.name] = &facility;
        for (const auto &recipe : facility.recipes)
            for (const auto &product : recipe.products)
                recipes_[product.item].push_back({&facility, &recipe});
    }
}
const Facility &RecipeGraph::facility(const std::string &name) const
{
    auto found = facilities_.find(name);
    if (found == facilities_.end())
        throw std::invalid_argument("Missing infrastructure facility: " + name);
    return *found->second;
}
bool RecipeGraph::is_resource(const std::string &item) const
{
    return resources_.count(item) != 0;
}
const Recipe *RecipeGraph::find_recipe(const std::string &item, const Facility *&facility) const
{
    facility = nullptr;
    if (selected_.count(item))
    {
        facility = selected_.at(item).first;
        return selected_.at(item).second;
    }
    auto found = recipes_.find(item);
    if (found == recipes_.end())
        return nullptr;
    if (found->second.size() != 1)
        throw std::invalid_argument("Ambiguous recipes for '" + item + "'");
    facility = found->second.front().first;
    return found->second.front().second;
}

std::vector<SeedLoopSpec> RecipeGraph::seed_loops() const
{
    std::vector<SeedLoopSpec> result;
    for (const auto &[seed, choices] : recipes_)
        for (const auto &[machine, picker] : choices)
        {
            if (machine->name != "seed_picking_unit" || !picker->unsupported_reasons.empty() ||
                picker->ingredients.size() != 1 || picker->products.size() != 1 ||
                picker->ingredients.front().amount != 1 || picker->products.front().amount != 2)
                continue;
            const auto flower = picker->ingredients.front().item;
            auto found = recipes_.find(flower);
            if (found == recipes_.end())
                continue;
            for (const auto &[planter_machine, planter] : found->second)
                if (planter_machine->name == "planting_unit" &&
                    planter->unsupported_reasons.empty() && planter->ingredients.size() == 1 &&
                    planter->products.size() == 1 && planter->ingredients.front().item == seed &&
                    planter->ingredients.front().amount == 1 &&
                    planter->products.front().amount == 1)
                {
                    result.push_back({seed, flower, picker, planter});
                    break;
                }
        }
    return result;
}

ProductionPlan RecipeGraph::plan_target(const std::string &target) const
{
    using namespace operations_research;
    using Choice = std::pair<const Facility *, const Recipe *>;
    const auto loops = seed_loops();
    const SeedLoopSpec *standalone = nullptr;
    for (const auto &loop : loops)
        if (loop.seed == target || loop.flower == target)
        {
            standalone = &loop;
            break;
        }
    std::vector<Choice> choices;
    std::set<std::string> visited, items;
    std::set<const Recipe *> included;
    std::string first_problem;
    std::function<void(const std::string &)> collect = [&](const std::string &item)
    {
        items.insert(item);
        const bool force_loop_flower =
            standalone && (standalone->flower == item || standalone->seed == item);
        if ((item != target && is_resource(item) && !force_loop_flower) ||
            !visited.insert(item).second)
            return;
        const auto found = recipes_.find(item);
        if (found == recipes_.end())
        {
            if (first_problem.empty())
                first_problem = "No recipe or declared resource supply for '" + item + "'";
            return;
        }
        if (!automatic_ && found->second.size() > 1)
            throw std::invalid_argument("Ambiguous recipes for '" + item +
                                        "': select one recipe in region.json");
        for (const auto &[machine, recipe] : found->second)
        {
            if (standalone && ((item == standalone->seed && recipe != standalone->picker) ||
                               (item == standalone->flower && recipe != standalone->planter)))
                continue;
            std::string problem;
            if (!recipe->unsupported_reasons.empty())
                problem = recipe->unsupported_reasons.front();
            if (recipe->products.size() != 1)
                problem = "multiple outputs";
            if (!recipe->has_port_bindings)
                for (const auto *ports : {&machine->inputs, &machine->outputs})
                    for (const auto &port : *ports)
                        if (port.type == TransportMedium::Pipe)
                            problem = "pipe transport";
            if (machine->inputs.empty() || machine->outputs.empty())
                problem = "missing production ports";
            if (!problem.empty())
            {
                if (first_problem.empty())
                    first_problem = recipe->name + ": " + problem;
                continue;
            }
            if (!included.insert(recipe).second)
                continue;
            choices.push_back({machine, recipe});
            for (const auto &ingredient : recipe->ingredients)
                collect(ingredient.item);
        }
    };
    collect(target);
    double target_rate = 0;
    for (const auto &[machine, recipe] : choices)
        if (recipe->products.front().item == target)
            target_rate = std::max(target_rate, recipe->products.front().amount / recipe->time);
    for (const auto &loop : loops)
        if (loop.seed == target || loop.flower == target)
        {
            // One seed per cycle returns to the paired planter; one is exported.
            target_rate = 1.0 / (loop.picker->time + loop.planter->time);
            break;
        }
    if (target_rate == 0)
        throw std::invalid_argument(first_problem.empty() ? "No recipe for '" + target + "'"
                                                          : first_problem);

    MPSolver solver("production_balance", MPSolver::GLOP_LINEAR_PROGRAMMING);
    std::map<std::string, MPConstraint *> balances;
    for (const auto &item : items)
    {
        const double required = item == target ? target_rate : 0;
        balances[item] = solver.MakeRowConstraint(required, required, item);
    }
    std::vector<MPVariable *> rates;
    for (const auto &[machine, recipe] : choices)
    {
        auto *rate = solver.MakeNumVar(0, solver.infinity(), recipe->name);
        rates.push_back(rate);
        std::map<std::string, double> coefficients;
        coefficients[recipe->products.front().item] += recipe->products.front().amount;
        for (const auto &ingredient : recipe->ingredients)
            coefficients[ingredient.item] -= ingredient.amount;
        for (const auto &[item, coefficient] : coefficients)
            balances.at(item)->SetCoefficient(rate, coefficient);
        // Minimize occupied machine capacity, with power as a small tie-breaker.
        solver.MutableObjective()->SetCoefficient(
            rate, recipe->time * (machine->width * machine->height + machine->power * 0.01));
    }
    std::map<std::string, MPVariable *> supplies;
    for (const auto &item : items)
        if (item != target && is_resource(item) &&
            !(standalone && (standalone->flower == item || standalone->seed == item)))
        {
            auto *supply = solver.MakeNumVar(0, solver.infinity(), item + "_external");
            supplies[item] = supply;
            balances.at(item)->SetCoefficient(supply, 1);
            solver.MutableObjective()->SetCoefficient(supply, 0.001);
        }
    solver.MutableObjective()->SetMinimization();
    if (solver.Solve() != MPSolver::OPTIMAL)
        throw std::invalid_argument("Infeasible production balance for '" + target + "'" +
                                    (first_problem.empty()
                                         ? ": recipe cycle cannot provide net output"
                                         : ": " + first_problem));
    ProductionPlan plan;
    plan.target_rate = target_rate;
    selected_.clear();
    for (size_t index = 0; index < choices.size(); ++index)
    {
        const double rate = rates[index]->solution_value();
        if (rate <= 1e-9)
            continue;
        const auto &[machine, recipe] = choices[index];
        const int count = static_cast<int>(std::ceil(rate * recipe->time - 1e-8));
        if (count > 200)
            throw std::invalid_argument("Production plan exceeds the supported machine budget");
        plan.steps.push_back({machine, recipe, rate, count, {}, SeedLoopRole::None});
        selected_[recipe->products.front().item] = {machine, recipe};
    }
    for (const auto &[item, supply] : supplies)
        if (supply->solution_value() > 1e-9)
            plan.external_rates[item] = supply->solution_value();

    // Size a crop-dependent candidate around one plant module per crop. The
    // regional optimizer can replicate the complete factory for more throughput.
    // This keeps the fixed modules from dominating a single routing problem.
    double module_scale = 1;
    for (const auto &loop : loops)
        for (const auto &step : plan.steps)
            if (step.recipe == loop.picker)
                module_scale = std::max(module_scale, step.cycles_per_second *
                                                          (loop.picker->time + loop.planter->time));
    if (module_scale > 1 + 1e-8)
    {
        plan.target_rate /= module_scale;
        for (auto &[item, rate] : plan.external_rates)
            rate /= module_scale;
        for (auto &step : plan.steps)
        {
            step.cycles_per_second /= module_scale;
            step.instances =
                static_cast<int>(std::ceil(step.cycles_per_second * step.recipe->time - 1e-8));
        }
    }

    // Turn recognized seed feedback into independent picker/planter modules.
    // A dedicated planter returns one flower; the picker reserves one seed for it
    // and exports the spare seed. Start each module with one flower in the picker.
    std::map<std::string, int> initial_flowers;
    for (const auto &loop : loops)
    {
        auto picker =
            std::find_if(plan.steps.begin(), plan.steps.end(),
                         [&](const ProductionStep &step) { return step.recipe == loop.picker; });
        auto planter =
            std::find_if(plan.steps.begin(), plan.steps.end(),
                         [&](const ProductionStep &step) { return step.recipe == loop.planter; });
        if (picker == plan.steps.end() || planter == plan.steps.end())
            continue;
        const double recycle_rate = picker->cycles_per_second;
        if (planter->cycles_per_second + 1e-8 < recycle_rate)
            throw std::invalid_argument("Seed loop lacks its recycling planter");
        const int modules = static_cast<int>(
            std::ceil(recycle_rate * (loop.picker->time + loop.planter->time) - 1e-8));
        picker->instances = modules;
        picker->seed_loop = loop.seed;
        picker->loop_role = SeedLoopRole::Picker;
        const ProductionStep paired{planter->facility, planter->recipe, recycle_rate,
                                    modules,           loop.seed,       SeedLoopRole::Planter};
        planter->cycles_per_second = std::max(0.0, planter->cycles_per_second - recycle_rate);
        planter->instances =
            static_cast<int>(std::ceil(planter->cycles_per_second * planter->recipe->time - 1e-8));
        // A second planter uses the spare seed and exports plants. Keep it in the
        // same fixed module rather than pooling seeds across distant machines.
        if (std::abs(planter->cycles_per_second - recycle_rate) < 1e-8 &&
            picker->facility->width == 5 && picker->facility->height == 5 &&
            planter->facility->width == 5 && planter->facility->height == 5)
        {
            planter->instances = modules;
            planter->seed_loop = loop.seed;
            planter->loop_role = SeedLoopRole::Harvest;
        }
        initial_flowers[loop.flower] += modules;
        // Append only after both iterators have been used; vector growth invalidates them.
        plan.steps.push_back(paired);
    }
    plan.steps.erase(std::remove_if(plan.steps.begin(), plan.steps.end(),
                                    [](const ProductionStep &step) { return step.instances <= 0; }),
                     plan.steps.end());

    // Productive cycles need initial stock even though their steady-state net input is zero.
    std::map<std::string, std::vector<std::string>> dependencies;
    for (const auto &step : plan.steps)
        for (const auto &ingredient : step.recipe->ingredients)
            if (!is_resource(ingredient.item))
                dependencies[step.recipe->products.front().item].push_back(ingredient.item);
    std::set<std::string> active, done, startup;
    std::vector<std::string> path;
    std::function<void(const std::string &)> visit = [&](const std::string &item)
    {
        if (active.count(item))
        {
            startup.insert(path.empty() ? item : path.back());
            return;
        }
        if (!done.insert(item).second)
            return;
        active.insert(item);
        path.push_back(item);
        for (const auto &dependency : dependencies[item])
            visit(dependency);
        path.pop_back();
        active.erase(item);
    };
    visit(target);
    plan.startup_items.assign(startup.begin(), startup.end());
    for (const auto &item : startup)
        for (const auto &step : plan.steps)
            for (const auto &ingredient : step.recipe->ingredients)
                if (ingredient.item == item)
                    plan.startup_stock[item] +=
                        step.instances * static_cast<int>(std::ceil(ingredient.amount));
    for (const auto &loop : loops)
        if (initial_flowers.count(loop.flower))
            plan.startup_stock.erase(loop.seed);
    for (const auto &[flower, count] : initial_flowers)
        plan.startup_stock[flower] = count;
    plan.startup_items.clear();
    for (const auto &[item, count] : plan.startup_stock)
        plan.startup_items.push_back(item);
    for (const auto &step : plan.steps)
        log(LogLevel::Debug, target, ": ", step.recipe->name, " x", step.instances, " at ",
            step.cycles_per_second, " cycles/s",
            step.seed_loop.empty() ? "" : " [closed seed loop: " + step.seed_loop + "]");
    return plan;
}

void RecipeGraph::validate_target(const std::string &target) const
{
    (void)plan_target(target);
}
} // namespace aic
