#include "blueprint_solver.hpp"
#include "cli.hpp"
#include "dataset.hpp"
#include "enkad_export.hpp"
#include "logging.hpp"
#include "solver.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

int main(int argc, char *argv[])
{
    using namespace aic;
    try
    {
        const auto options = parse_cli(argc, argv);
        log_level = options.verbosity;
        if (options.command == Command::Help)
        {
            std::cout << cli_usage();
            return 0;
        }
        Dataset data = Dataset::load(options.folder);
        if (!options.target.empty() && options.target_kind == TargetKind::SaleProduct)
        {
            const auto found = std::find_if(data.products.begin(), data.products.end(),
                                            [&](const Product &product)
                                            { return product.name == options.target; });
            if (found == data.products.end())
                throw std::invalid_argument("Unknown sale product: " + options.target);
            data.products = {*found};
        }
        const bool wuling = data.region.market_region == "domain_2";
        const auto configured_area = [](const Area &area)
        { return area.pac_width > 0 && area.pac_height > 0; };
        std::vector<const Area *> area_contexts;
        if (wuling)
            for (const auto &area : data.areas)
                if (configured_area(area))
                    area_contexts.push_back(&area);
        if (!wuling)
            area_contexts.push_back(nullptr);
        RecipeGraph graph(data.facilities, data.region.resources, data.automatic_recipes,
                          area_contexts.empty() ? nullptr : area_contexts.front());
        if (options.command == Command::ListSeedLoops)
        {
            std::cout << "Seed output | Starter flower type\n";
            for (const auto &loop : graph.seed_loops())
                std::cout << loop.seed << " | " << loop.flower << '\n';
            return 0;
        }
        if (!options.target.empty() && options.target_kind != TargetKind::SaleProduct)
        {
            const auto loops = graph.seed_loops();
            const auto found = std::find_if(
                loops.begin(), loops.end(),
                [&](const SeedLoopSpec &loop) {
                    return options.target ==
                           (options.target_kind == TargetKind::Plant ? loop.flower : loop.seed);
                });
            if (found == loops.end())
                throw std::invalid_argument("Unknown plant/seed loop; use --list-seed-loops");
            Product target;
            target.name = options.target;
            data.products = {target};
        }
        if (options.command == Command::ListProducts)
        {
            std::cout << "Sellable products (price per item"
                      << (data.region.currency.empty() ? "" : ", " + data.region.currency) << ")\n";
            for (const auto &product : data.products)
                std::cout << product.name << ": " << product.value << '\n';
            return 0;
        }
        if (area_contexts.empty())
            throw std::invalid_argument(
                "Wuling areas.json needs an area with pac_width and pac_height");
        if (wuling && data.region.storage <= 0)
            throw std::invalid_argument("Wuling region.json needs a positive storage value");
        if (wuling && !data.region.base_power_configured)
            throw std::invalid_argument("Wuling region.json needs an explicit base_power value");
        if (!options.target.empty())
            data.region.rebuilding_orders.clear();
        for (const auto &order : data.region.rebuilding_orders)
        {
            Product target;
            target.name = order.first;
            target.rebuilding = true;
            data.products.push_back(std::move(target));
        }
        if (options.command == Command::Validate)
        {
            size_t supported = 0;
            for (const auto &product : data.products)
            {
                bool feasible = false;
                for (const auto *area : area_contexts)
                {
                    if (product.rebuilding && (!area || !area->rebuilding_port))
                        continue;
                    try
                    {
                        RecipeGraph candidate(data.facilities, data.region.resources,
                                              data.automatic_recipes, area);
                        candidate.validate_target(product.name);
                        feasible = true;
                        break;
                    }
                    catch (const std::invalid_argument &error)
                    {
                        if (!data.automatic_recipes && !wuling)
                            throw;
                        log(LogLevel::Warning, product.name, area ? " in " + area->name : "", ": ",
                            error.what());
                    }
                }
                supported += feasible;
            }
            log(LogLevel::Info, supported, " of ", data.products.size(),
                " targets have balanced production plans");
            log(LogLevel::Info, "Dataset valid; supported target dependencies checked");
            return 0;
        }
        std::map<std::string, double> facility_powers;
        for (const auto &facility : data.facilities)
            facility_powers[facility.name] = facility.power;
        BlueprintOptions blueprint_options;
        blueprint_options.automatic_recipes = data.automatic_recipes;
        blueprint_options.placement_seconds = options.placement_seconds;
        blueprint_options.compact_seconds = options.compact_seconds;
        blueprint_options.allow_overpasses = options.enkad_directory.empty();
        blueprint_options.blueprint_size = options.blueprint_size;
        std::vector<std::unique_ptr<BlueprintSolver>> blueprint_solvers;
        for (const auto *area : area_contexts)
        {
            blueprint_options.local_area = area;
            blueprint_solvers.push_back(std::make_unique<BlueprintSolver>(
                data.facilities, data.region.resources, data.areas, blueprint_options));
        }
        if (!options.enkad_directory.empty())
            std::filesystem::create_directories(options.enkad_directory);
        size_t exported = 0;
        std::set<std::string> generated;
        std::vector<Product> producible_products;
        std::map<std::string, BlueprintLayout> layouts;
        log(LogLevel::Info, "Generating factory blueprints for ", data.products.size(),
            " product candidates");
        if (options.blueprint_size)
            log(LogLevel::Info, "Blueprint placement limit: ", options.blueprint_size->first, " x ",
                options.blueprint_size->second);
        const auto generate = [&](const Product &target, bool reuse_only)
        {
            bool target_exported = false;
            for (size_t index = 0; index < blueprint_solvers.size(); ++index)
            {
                auto product = target;
                if (const auto *area = area_contexts[index])
                    product.candidate_area = area->name;
                BlueprintLayout layout;
                try
                {
                    if (!blueprint_solvers[index]->solve(product, layout, reuse_only))
                        continue;
                }
                catch (const std::invalid_argument &error)
                {
                    if (!reuse_only)
                        log(LogLevel::Warning, "Skipping ", product.layout_key(), ": ",
                            error.what());
                    continue;
                }
                generated.insert(target.layout_key());
                log(LogLevel::Info, "Blueprint generated for ", product.layout_key(), " at ",
                    layout.output_per_minute, " units/min");
                if (!options.no_layouts)
                    layout.print();
                if (!options.enkad_directory.empty() && !target_exported)
                    try
                    {
                        const auto destination =
                            write_enkad_blueprint(options.enkad_directory, layout, data.facilities);
                        ++exported;
                        target_exported = true;
                        log(LogLevel::Info, "EnKAD export: ", destination.string());
                    }
                    catch (const std::exception &error)
                    {
                        log(LogLevel::Error, "Could not export ", product.name, ": ", error.what());
                    }
                layouts.emplace(product.layout_key(), std::move(layout));
                producible_products.push_back(std::move(product));
            }
        };
        for (const auto &target : data.products)
            generate(target, false);
        if (!wuling)
            for (const auto &target : data.products)
                if (!generated.count(target.layout_key()))
                    generate(target, true);

        log(LogLevel::Info, "Blueprints generated: ", generated.size(), "/", data.products.size());
        if (producible_products.empty())
            throw std::runtime_error("No supported, successfully routed target blueprints");
        const bool blueprints_complete = generated.size() == data.products.size();
        const bool exports_complete =
            options.enkad_directory.empty() || exported == data.products.size();
        if (!options.enkad_directory.empty())
            log(exports_complete ? LogLevel::Info : LogLevel::Warning,
                "EnKAD blueprints exported: ", exported, "/", data.products.size());
        if (options.blueprints_only)
            return blueprints_complete && exports_complete ? 0 : 1;
        log(LogLevel::Info, "Optimizing regional production");
        Solver solver(producible_products, data.region.resources, data.areas, data.fuels,
                      facility_powers, data.region);
        solver.set_blueprint_layouts(layouts);
        solver.set_time_limit(options.solve_seconds);
        solver.set_render_layouts(!options.no_layouts);
        return solver.solve() && exports_complete ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        log(LogLevel::Error, error.what());
        return 1;
    }
}
