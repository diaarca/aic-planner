#include "recipe_graph.hpp"
#include "logging.hpp"
#include "ortools/linear_solver/linear_solver.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace aic
{
std::string array_mode(const Recipe &recipe)
{
    if (recipe.id.rfind("formation_magic_ming_", 0) == 0)
        return "L";
    if (recipe.id.rfind("formation_magic_hui_", 0) == 0)
        return "T";
    if (recipe.id.rfind("formation_magic_heng_", 0) == 0)
        return "E";
    return {};
}

static double seed_loop_period(const SeedLoopSpec &loop)
{
    return std::max(
        {loop.picker->time, loop.planter->time, static_cast<double>(solid_belt_seconds_per_item)});
}

RecipeGraph::RecipeGraph(const std::vector<Facility> &facilities,
                         const std::vector<Mineral> &resources, bool automatic, const Area *area)
    : area_(area), automatic_(automatic)
{
    for (const auto &resource : resources)
        resources_.insert(resource.name);
    if (area_)
        for (const auto &source : area_->sources)
            local_limits_[source.item] += source.limit / 60;
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
    return resources_.count(item) != 0 || local_limits_.count(item) != 0;
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
                picker->ingredients.front().amount != 1)
                continue;
            const auto flower = picker->ingredients.front().item;
            auto found = recipes_.find(flower);
            if (found == recipes_.end())
                continue;
            for (const auto &[planter_machine, planter] : found->second)
            {
                if (planter_machine->name != "planting_unit" || planter->products.size() != 1)
                    continue;
                const auto &seed_name = seed;
                const auto seed_input =
                    std::find_if(planter->ingredients.begin(), planter->ingredients.end(),
                                 [&](const RecipeIngredient &ingredient) {
                                     return ingredient.item == seed_name && ingredient.amount == 1;
                                 });
                if (seed_input == planter->ingredients.end())
                    continue;
                const bool dry =
                    planter->unsupported_reasons.empty() && planter->ingredients.size() == 1 &&
                    picker->products.front().amount == 2 && planter->products.front().amount == 1;
                const bool wet =
                    area_ && planter->ingredients.size() == 2 &&
                    std::any_of(planter->ingredients.begin(), planter->ingredients.end(),
                                [](const RecipeIngredient &input)
                                { return input.phase == MaterialPhase::Liquid; }) &&
                    picker->products.front().amount * planter->products.front().amount > 1;
                if (dry || wet)
                {
                    result.push_back({seed, flower, picker, planter, wet});
                    break;
                }
            }
        }
    return result;
}

ProductionPlan RecipeGraph::plan_target(const std::string &target,
                                        const std::set<const Recipe *> &excluded_final) const
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
    std::map<std::string, MaterialPhase> phases;
    std::set<const Recipe *> included;
    std::string first_problem;
    const auto climate_for = [&](const Recipe &recipe) -> const AreaClimate *
    {
        if (!area_ || recipe.environment == 0)
            return nullptr;
        const auto found = area_->climates.find(recipe.environment);
        return found == area_->climates.end() ? nullptr : &found->second;
    };
    const auto field_for = [&](const Recipe &recipe) -> const ArrayField *
    {
        if (!area_)
            return nullptr;
        const auto found = area_->array_fields.find(array_mode(recipe));
        return found == area_->array_fields.end() ? nullptr : &found->second;
    };
    const auto cycle_time = [&](const Recipe &recipe)
    {
        const auto *field = field_for(recipe);
        return field ? std::max(recipe.time, field->cycle_seconds) : recipe.time;
    };
    std::function<void(const std::string &)> collect = [&](const std::string &item)
    {
        items.insert(item);
        const bool force_loop_flower =
            standalone && (standalone->flower == item || standalone->seed == item);
        if ((item != target && resources_.count(item) && !force_loop_flower) ||
            !visited.insert(item).second)
            return;
        const auto found = recipes_.find(item);
        if (found == recipes_.end())
        {
            if (first_problem.empty() && !local_limits_.count(item))
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
            for (const auto &reason : recipe->unsupported_reasons)
                if (!area_ || (reason != "pipe transport" && reason != "multiple outputs" &&
                               reason != "multiple buffer groups" &&
                               !(reason == "environment requirement" && climate_for(*recipe)) &&
                               !(reason == "array projection" && field_for(*recipe))))
                {
                    problem = reason;
                    break;
                }
            if (!area_ && recipe->products.size() != 1)
                problem = "multiple outputs";
            if (recipe->environment != 0 && !climate_for(*recipe))
                problem = "missing climate configuration for environment " +
                          std::to_string(recipe->environment);
            if (!array_mode(*recipe).empty() && !field_for(*recipe))
                problem = "missing " + array_mode(*recipe) + "-Array geometry in areas.json";
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
            for (const auto &product : recipe->products)
            {
                items.insert(product.item);
                phases[product.item] = product.phase;
            }
            for (const auto &ingredient : recipe->ingredients)
            {
                phases[ingredient.item] = ingredient.phase;
                collect(ingredient.item);
            }
            if (area_ && machine->activation)
            {
                phases[machine->activation->item] = machine->activation->phase;
                collect(machine->activation->item);
            }
            if (const auto *climate = climate_for(*recipe))
            {
                phases[climate->gas] = MaterialPhase::Gas;
                collect(climate->gas);
            }
            if (field_for(*recipe))
            {
                phases["item_gas_xiranite"] = MaterialPhase::Gas;
                collect("item_gas_xiranite");
            }
        }
    };
    collect(target);
    std::vector<size_t> final_recipes;
    const auto output_amount = [&](const Recipe &recipe)
    {
        double amount = 0;
        for (const auto &product : recipe.products)
            if (product.item == target)
                amount += product.amount;
        return amount;
    };
    for (size_t index = 0; index < choices.size(); ++index)
        if (output_amount(*choices[index].second) > 0 &&
            !excluded_final.count(choices[index].second))
            final_recipes.push_back(index);
    if (final_recipes.empty())
        throw std::invalid_argument(first_problem.empty() ? "No recipe for '" + target + "'"
                                                          : first_problem);
    const auto nominal_output = [&](size_t index)
    {
        const auto &recipe = *choices[index].second;
        return output_amount(recipe) / cycle_time(recipe);
    };
    std::stable_sort(final_recipes.begin(), final_recipes.end(),
                     [&](size_t a, size_t b)
                     {
                         if (std::abs(nominal_output(a) - nominal_output(b)) > 1e-9)
                             return nominal_output(a) > nominal_output(b);
                         if (area_)
                             return (choices[a].first->activation.has_value() +
                                     !!climate_for(*choices[a].second)) <
                                    (choices[b].first->activation.has_value() +
                                     !!climate_for(*choices[b].second));
                         return false;
                     });

    const bool fixed_feeds = area_ && std::any_of(choices.begin(), choices.end(),
                                                  [&](const Choice &choice) {
                                                      return choice.first->activation ||
                                                             climate_for(*choice.second) ||
                                                             field_for(*choice.second);
                                                  });
    MPSolver solver("production_balance", fixed_feeds ? MPSolver::CBC_MIXED_INTEGER_PROGRAMMING
                                                      : MPSolver::GLOP_LINEAR_PROGRAMMING);
    if (fixed_feeds)
        solver.SetTimeLimit(absl::Seconds(4));
    std::map<std::string, MPConstraint *> balances;
    for (const auto &item : items)
        balances[item] = solver.MakeRowConstraint(0, 0, item);
    std::vector<MPVariable *> rates;
    std::map<const Recipe *, MPVariable *> counted;
    for (const auto &[machine, recipe] : choices)
    {
        auto *rate = solver.MakeNumVar(0, solver.infinity(), recipe->name);
        rates.push_back(rate);
        std::map<std::string, double> coefficients;
        for (const auto &product : recipe->products)
            coefficients[product.item] += product.amount;
        for (const auto &ingredient : recipe->ingredients)
            coefficients[ingredient.item] -= ingredient.amount;
        for (const auto &[item, coefficient] : coefficients)
            balances.at(item)->SetCoefficient(rate, coefficient);
        // Minimize occupied machine capacity, with power as a small tie-breaker.
        solver.MutableObjective()->SetCoefficient(
            rate, cycle_time(*recipe) * (machine->width * machine->height + machine->power * 0.01));
        const auto *climate = climate_for(*recipe);
        const auto *field = field_for(*recipe);
        if (area_ && (machine->activation || climate || field))
        {
            auto *instances = solver.MakeIntVar(0, 200, recipe->id + "_instances");
            auto *capacity = solver.MakeRowConstraint(-solver.infinity(), 0);
            capacity->SetCoefficient(rate, cycle_time(*recipe));
            capacity->SetCoefficient(instances, -1);
            auto *used = solver.MakeRowConstraint(-solver.infinity(), 1 - 1e-6);
            used->SetCoefficient(instances, 1);
            used->SetCoefficient(rate, -cycle_time(*recipe));
            std::map<std::string, double> feeds;
            if (machine->activation)
                feeds[machine->activation->item] += machine->activation->rate;
            if (climate)
                feeds[climate->gas] += climate->limit / 60;
            if (field)
                for (const auto &projector : field->projectors)
                    feeds["item_gas_xiranite"] += projector.feed / 60;
            for (const auto &[item, amount] : feeds)
                balances.at(item)->SetCoefficient(instances, -amount);
            solver.MutableObjective()->SetCoefficient(instances, 0.01);
            counted[recipe] = instances;
        }
    }
    std::map<std::string, std::pair<const Facility *, MPVariable *>> disposal;
    if (area_)
        for (const auto &[name, machine] : facilities_)
            for (const auto &[item, capacity] : machine->disposal_rates)
                if (item != target && balances.count(item) && !disposal.count(item))
                {
                    auto *sink = solver.MakeNumVar(0, solver.infinity(), item + "_disposal");
                    balances.at(item)->SetCoefficient(sink, -1);
                    solver.MutableObjective()->SetCoefficient(
                        sink,
                        (machine->width * machine->height + machine->power * 0.01) / capacity);
                    disposal.emplace(item, std::make_pair(machine, sink));
                }
    std::map<std::string, MPVariable *> supplies;
    for (const auto &item : items)
        if (item != target && is_resource(item) &&
            !(standalone && (standalone->flower == item || standalone->seed == item)))
        {
            const auto local = local_limits_.find(item);
            const double limit = local == local_limits_.end() ? solver.infinity() : local->second;
            auto *supply = solver.MakeNumVar(0, limit, item + "_external");
            supplies[item] = supply;
            balances.at(item)->SetCoefficient(supply, 1);
            solver.MutableObjective()->SetCoefficient(supply, 0.001);
        }
    solver.MutableObjective()->SetMinimization();
    double target_rate = 0;
    const Recipe *selected_final = nullptr;
    if (standalone)
    {
        double rate = 1.0 / seed_loop_period(*standalone);
        if (standalone->hydroponic)
        {
            const bool harvesting = standalone->flower == target;
            const auto *final = harvesting ? standalone->planter : standalone->picker;
            const auto *return_recipe = harvesting ? standalone->picker : standalone->planter;
            const auto index =
                std::find_if(choices.begin(), choices.end(),
                             [&](const Choice &choice) { return choice.second == final; }) -
                choices.begin();
            const double cycles = 1.0 / final->time;
            const double returned = cycles / return_recipe->products.front().amount;
            rate = output_amount(*final) * cycles - returned;
            rates.at(index)->SetBounds(cycles, cycles);
        }
        balances.at(target)->SetBounds(rate, rate);
        if (solver.Solve() == MPSolver::OPTIMAL)
            target_rate = rate;
    }
    else
    {
        // Select one final recipe at full speed, trying the highest output first.
        for (size_t index = 0; index < choices.size(); ++index)
            if (output_amount(*choices[index].second) > 0)
                rates[index]->SetBounds(0, 0);
        for (const auto index : final_recipes)
        {
            const double cycles = 1.0 / cycle_time(*choices[index].second);
            const double output = nominal_output(index);
            rates[index]->SetBounds(cycles, cycles);
            balances.at(target)->SetBounds(output, output);
            if (solver.Solve() == MPSolver::OPTIMAL)
            {
                target_rate = output;
                selected_final = choices[index].second;
                break;
            }
            rates[index]->SetBounds(0, 0);
        }
    }
    if (target_rate == 0)
        throw std::invalid_argument("Infeasible production balance for '" + target + "'" +
                                    (first_problem.empty()
                                         ? ": recipe cycle cannot provide net output"
                                         : ": " + first_problem));
    ProductionPlan plan;
    plan.target_rate = target_rate;
    plan.area = area_;
    plan.final_recipe = selected_final;
    plan.phases = std::move(phases);
    if (area_)
    {
        for (const auto &source : area_->sources)
            plan.source_facilities.emplace(source.facility, &facility(source.facility));
        if (facilities_.count("log_pipe_splitter"))
            plan.pipe_splitter = &facility("log_pipe_splitter");
        if (facilities_.count("log_pipe_converger"))
            plan.pipe_converger = &facility("log_pipe_converger");
        if (facilities_.count("log_pipe_conditioner"))
            plan.pipe_valve = &facility("log_pipe_conditioner");
    }
    selected_.clear();
    for (size_t index = 0; index < choices.size(); ++index)
    {
        const double rate = rates[index]->solution_value();
        if (rate <= 1e-9)
            continue;
        const auto &[machine, recipe] = choices[index];
        const int count = counted.count(recipe)
                              ? static_cast<int>(std::llround(counted.at(recipe)->solution_value()))
                              : static_cast<int>(std::ceil(rate * recipe->time - 1e-8));
        if (count > 200)
            throw std::invalid_argument("Production plan exceeds the supported machine budget");
        plan.steps.push_back({machine, recipe, rate, count, {}, SeedLoopRole::None});
        for (const auto &product : recipe->products)
            selected_[product.item] = {machine, recipe};
        if (climate_for(*recipe))
            plan.disperser = &facility("vaporizer_1");
        if (field_for(*recipe))
            plan.projector = &facility("magic_spawner_1");
    }
    for (const auto &[item, supply] : supplies)
        if (supply->solution_value() > 1e-9)
            plan.external_rates[item] = supply->solution_value();
    for (const auto &[item, sink] : disposal)
        if (sink.second->solution_value() > 1e-9)
            plan.disposal.emplace(item, std::make_pair(sink.first, sink.second->solution_value()));

    // Turn recognized seed feedback into independent picker/planter modules.
    // A dedicated planter returns one flower; the picker reserves one seed for it
    // and exports the spare seed.
    std::map<std::string, int> initial_flowers;
    for (const auto &loop : loops)
    {
        if (loop.hydroponic)
            continue;
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
        const int modules =
            std::max(1, static_cast<int>(std::ceil(recycle_rate * seed_loop_period(loop) - 1e-8)));
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
        // Stock the concurrent recipe cycles. A routed loop may also carry stock in transit.
        const int flowers_per_picker = std::max(
            1, static_cast<int>(std::ceil(
                   recycle_rate / modules * (loop.picker->time + loop.planter->time) - 1e-8)));
        initial_flowers[loop.flower] += modules * flowers_per_picker;
        // Append only after both iterators have been used; vector growth invalidates them.
        plan.steps.push_back(paired);
    }
    plan.steps.erase(std::remove_if(plan.steps.begin(), plan.steps.end(),
                                    [](const ProductionStep &step) { return step.instances <= 0; }),
                     plan.steps.end());

    // A producer needs enough output ports to reach its downstream machines.
    // The flow allocator still checks the exact connections and input capacity.
    std::map<std::string, int> incoming_belts, producing_steps;
    for (const auto &step : plan.steps)
    {
        ++producing_steps[step.recipe->products.front().item];
        std::map<std::string, double> demand;
        for (const auto &ingredient : step.recipe->ingredients)
            demand[ingredient.item] += ingredient.amount * step.cycles_per_second / step.instances;
        for (const auto &[item, rate] : demand)
            incoming_belts[item] +=
                step.instances *
                static_cast<int>(std::ceil(rate / transport_capacity(plan.phases.at(item)) - 1e-8));
    }
    for (auto &step : plan.steps)
    {
        const auto &item = step.recipe->products.front().item;
        if (counted.count(step.recipe) || step.recipe->products.size() != 1 ||
            step.loop_role != SeedLoopRole::None || producing_steps.at(item) != 1 ||
            plan.external_rates.count(item))
            continue;
        const auto medium = step.recipe->products.front().phase == MaterialPhase::Solid
                                ? TransportMedium::Solid
                                : TransportMedium::Pipe;
        const int ports =
            std::count_if(step.facility->outputs.begin(), step.facility->outputs.end(),
                          [medium](const Port &port) { return port.type == medium; });
        if (ports > 0)
            step.instances = std::max(step.instances, (incoming_belts[item] + ports - 1) / ports);
    }

    for (const auto &step : plan.steps)
        if (output_amount(*step.recipe) > 0 && step.loop_role != SeedLoopRole::Planter &&
            std::abs(step.cycles_per_second * cycle_time(*step.recipe) - step.instances) > 1e-8)
            throw std::invalid_argument("Final facility for '" + target +
                                        "' cannot run at full utilization");

    // Productive cycles need initial stock even though their steady-state net input is zero.
    std::map<std::string, std::vector<std::string>> dependencies;
    for (const auto &step : plan.steps)
        for (const auto &product : step.recipe->products)
            for (const auto &ingredient : step.recipe->ingredients)
                if (!is_resource(ingredient.item))
                    dependencies[product.item].push_back(ingredient.item);
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
