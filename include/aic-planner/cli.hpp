#pragma once

#include "logging.hpp"
#include <string>

namespace aic
{
enum class Command
{
    Plan,
    Validate,
    ListProducts,
    ListSeedLoops,
    Help
};
enum class TargetKind
{
    SaleProduct,
    Seed,
    Plant
};
struct CliOptions
{
    Command command = Command::Plan;
    TargetKind target_kind = TargetKind::SaleProduct;
    std::string folder;
    std::string target;
    bool no_layouts = false;
    bool blueprints_only = false;
    double solve_seconds = 30.0;
    LogLevel verbosity = LogLevel::Info;
};
CliOptions parse_cli(int argc, const char *const argv[]);
std::string cli_usage();
} // namespace aic
