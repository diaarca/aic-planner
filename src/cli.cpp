#include "cli.hpp"
#include <cmath>
#include <stdexcept>

namespace aic
{
std::string cli_usage()
{
    return "Usage: aic_planner [--log-level error|warning|info|debug] [--validate] [--no-layouts] "
           "[--list-products] [--product name] [--blueprints-only] [--solve-seconds seconds] "
           "[--seed-loop seed] [--plant-loop plant] [--list-seed-loops] <data_folder>\n";
}

CliOptions parse_cli(int argc, const char *const argv[])
{
    CliOptions options;
    auto set_command = [&](Command command)
    {
        if (options.command != Command::Plan && options.command != command)
            throw std::invalid_argument(
                "Choose one of --validate, --list-products, or --list-seed-loops");
        options.command = command;
    };
    auto set_target = [&](TargetKind kind, const std::string &target)
    {
        if (!options.target.empty())
            throw std::invalid_argument(
                "Choose one target: --product, --seed-loop, or --plant-loop");
        options.target_kind = kind;
        options.target = target;
    };
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        auto value = [&]() -> std::string
        {
            if (index + 1 == argc || std::string(argv[index + 1]).rfind("--", 0) == 0)
                throw std::invalid_argument("Missing value for " + argument);
            const std::string result = argv[++index];
            if (result.empty())
                throw std::invalid_argument("Empty value for " + argument);
            return result;
        };
        if (argument == "--help")
        {
            options.command = Command::Help;
            return options;
        }
        if (argument == "--log-level")
            options.verbosity = parse_log_level(value());
        else if (argument == "--solve-seconds")
        {
            const auto text = value();
            size_t parsed = 0;
            try
            {
                options.solve_seconds = std::stod(text, &parsed);
            }
            catch (const std::logic_error &)
            {
                throw std::invalid_argument("--solve-seconds must be a positive finite number");
            }
            if (parsed != text.size() || !std::isfinite(options.solve_seconds) ||
                options.solve_seconds <= 0)
                throw std::invalid_argument("--solve-seconds must be a positive finite number");
        }
        else if (argument == "--product")
            set_target(TargetKind::SaleProduct, value());
        else if (argument == "--seed-loop")
            set_target(TargetKind::Seed, value());
        else if (argument == "--plant-loop")
            set_target(TargetKind::Plant, value());
        else if (argument == "--list-products")
            set_command(Command::ListProducts);
        else if (argument == "--list-seed-loops")
            set_command(Command::ListSeedLoops);
        else if (argument == "--validate")
            set_command(Command::Validate);
        else if (argument == "--no-layouts")
            options.no_layouts = true;
        else if (argument == "--blueprints-only")
            options.blueprints_only = true;
        else if (!argument.empty() && argument.front() != '-' && options.folder.empty())
            options.folder = argument;
        else
            throw std::invalid_argument("Unknown argument: " + argument);
    }
    if (options.folder.empty())
        throw std::invalid_argument("Missing data folder; use --help for usage");
    if (options.target_kind != TargetKind::SaleProduct)
    {
        if (options.command != Command::Plan)
            throw std::invalid_argument(
                "A plant/seed loop cannot be combined with listing or validation");
        options.blueprints_only = true;
    }
    return options;
}
} // namespace aic
