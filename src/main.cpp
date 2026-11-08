#include "blueprint_solver.hpp"
#include "cli.hpp"
#include "dataset.hpp"
#include "logging.hpp"
#include "solver.hpp"
#include <algorithm>
#include <iostream>
#include <map>
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
        RecipeGraph graph(data.facilities, data.region.resources, data.automatic_recipes);
        if (options.command == Command::ListSeedLoops)
        {
            std::cout << "Seed output | Initial flower in seed-picking unit (once)\n";
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
        if (options.command == Command::Validate)
        {
            size_t supported = 0;
            for (const auto &product : data.products)
                try
                {
                    graph.validate_target(product.name);
                    ++supported;
                }
                catch (const std::invalid_argument &error)
                {
                    if (!data.automatic_recipes)
                        throw;
                    log(LogLevel::Warning, product.name, ": ", error.what());
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
        BlueprintSolver blueprint_solver(data.facilities, data.region.resources, data.areas,
                                         blueprint_options);
        std::vector<Product> producible_products;
        std::map<std::string, BlueprintLayout> layouts;
        log(LogLevel::Info, "Generating factory blueprints for ", data.products.size(),
            " product candidates");
        for (auto product : data.products)
        {
            try
            {
                BlueprintLayout layout;
                if (blueprint_solver.solve(product, layout))
                {
                    log(LogLevel::Info, "Blueprint generated for ", product.name);
                    if (!options.no_layouts)
                        layout.print();
                    layouts[product.name] = std::move(layout);
                    producible_products.push_back(product);
                }
            }
            catch (const std::invalid_argument &error)
            {
                log(LogLevel::Warning, "Skipping ", product.name, ": ", error.what());
            }
        }
        log(LogLevel::Info, "Blueprints generated: ", producible_products.size(), "/",
            data.products.size());
        if (producible_products.empty())
            throw std::runtime_error("No supported, successfully routed target blueprints");
        if (options.blueprints_only)
            return producible_products.size() == data.products.size() ? 0 : 1;
        log(LogLevel::Info, "Optimizing regional production");
        Solver solver(producible_products, data.region.resources, data.areas, data.fuels,
                      facility_powers, data.region);
        solver.set_blueprint_layouts(layouts);
        solver.set_time_limit(options.solve_seconds);
        solver.set_render_layouts(!options.no_layouts);
        return solver.solve() ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        log(LogLevel::Error, error.what());
        return 1;
    }
}
