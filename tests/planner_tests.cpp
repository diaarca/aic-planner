#include "blueprint_solver.hpp"
#include "cli.hpp"
#include "dataset.hpp"
#include "logging.hpp"
#include "production_network.hpp"
#include "solver.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace aic;
void check(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &action, const std::string &expected)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument &error)
    {
        check(std::string(error.what()).find(expected) != std::string::npos,
              "Unexpected diagnostic: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Expected rejection: " + expected);
}
void command_line()
{
    auto parse = [](std::initializer_list<const char *> arguments)
    {
        std::vector<const char *> argv{"aic_planner"};
        argv.insert(argv.end(), arguments);
        return parse_cli(static_cast<int>(argv.size()), argv.data());
    };
    check(parse({"--help"}).command == Command::Help, "Help must work without a dataset");
    const auto crop = parse({"--plant-loop", "sandleaf", "--no-layouts", "data/valley_iv"});
    check(crop.target_kind == TargetKind::Plant && crop.target == "sandleaf" &&
              crop.blueprints_only && crop.no_layouts,
          "Plant helper must select a blueprint target without starting regional optimization");
    check(parse({"--validate", "--product", "ferrium_part", "data/test"}).command ==
              Command::Validate,
          "Product selection remains available during validation");
    rejects([&] { parse({"--product", "--no-layouts", "data/test"}); }, "Missing value");
    rejects(
        [&] {
            parse({"--seed-loop", "sandleaf_seed", "--plant-loop", "sandleaf", "data/valley_iv"});
        },
        "Choose one target");
    rejects([&] { parse({"--list-products", "--validate", "data/test"}); }, "Choose one");
    rejects(
        [&] {
            parse({"--plant-loop", "sandleaf", "--list-seed-loops", "data/valley_iv"});
        },
        "cannot be combined");
    for (const auto *seconds : {"0", "-1", "nan", "inf", "1e999", "invalid", "3s"})
        rejects([&] { parse({"--solve-seconds", seconds, "data/test"}); }, "positive finite");
}

void layout_geometry()
{
    PlacedBlueprintEntity entity;
    entity.x = 1;
    entity.y = 1;
    entity.width = 3;
    entity.height = 1;
    entity.ports.push_back({1, 1, true, 0, -1, TransportMedium::Solid, "ore"});
    auto rotated = rotate_entity(entity, 9, 6, 90);
    check(rotated.x == 1 && rotated.y == 5 && rotated.width == 1 && rotated.height == 3,
          "A non-square entity rotates within its containing blueprint");
    check(rotated.ports[0].x == 1 && rotated.ports[0].y == 7 && rotated.ports[0].fx == -1 &&
              rotated.ports[0].fy == 0,
          "Port positions and outward normals rotate together");
    int width = 6, height = 9;
    for (int turn = 1; turn < 4; ++turn)
    {
        rotated = rotate_entity(rotated, width, height, 90);
        std::swap(width, height);
    }
    check(rotated.x == entity.x && rotated.y == entity.y && rotated.width == entity.width &&
              rotated.height == entity.height && rotated.ports[0].x == entity.ports[0].x &&
              rotated.ports[0].y == entity.ports[0].y && rotated.ports[0].fx == 0 &&
              rotated.ports[0].fy == -1 && rotated.ports[0].item == "ore",
          "Four quarter turns preserve geometry and item metadata");
    check(rotate_conveyor_mask(CONV_NORTH | CONV_WEST, 90) == (CONV_EAST | CONV_NORTH),
          "Conveyor corners rotate with the entities");
    rejects([&] { rotate_entity(entity, 9, 6, 45); }, "Rotation");
    BlueprintLayout layout;
    entity.x = -3;
    entity.y = 4;
    entity.width = 2;
    entity.height = 3;
    entity.ports[0].x = -3;
    entity.ports[0].y = 4;
    layout.entities.push_back(entity);
    layout.conveyor_cells[{-4, 5}] = CONV_EAST | CONV_WEST;
    layout.conveyor_items[{-4, 5}] = "ore";
    layout.normalize();
    check(layout.bounding_width == 3 && layout.bounding_height == 3 && layout.entities[0].x == 1 &&
              layout.entities[0].y == 0 && layout.entities[0].ports[0].x == 1 &&
              layout.conveyor_items.at({0, 1}) == "ore",
          "Normalization includes belts and shifts ports and item metadata with their entities");
}

void dependencies(const Dataset &fixture)
{
    RecipeGraph graph(fixture.facilities, fixture.region.resources);
    graph.validate_target("sc_valley_battery");
    rejects([&] { graph.validate_target("unknown_ingredient"); }, "declared resource");
    auto facilities = fixture.facilities;
    facilities[0].recipes.push_back(facilities[0].recipes.front());
    RecipeGraph ambiguous(facilities, fixture.region.resources);
    rejects([&] { ambiguous.validate_target("ferrium_part"); }, "Ambiguous recipes");
    facilities[0].recipes.back().ingredients[0].item = "missing_ingredient";
    RecipeGraph automatic(facilities, fixture.region.resources, true);
    automatic.validate_target("ferrium_part");
    const Facility *chosen_facility = nullptr;
    const Recipe *chosen = automatic.find_recipe("ferrium", chosen_facility);
    check(chosen && chosen->ingredients[0].item == "ferrium_ore",
          "Automatic selection must skip unusable alternatives rather than depending on source "
          "order");
    facilities = fixture.facilities;
    facilities[0].recipes[0].ingredients[0].item = "ferrium_part";
    RecipeGraph cyclic(facilities, fixture.region.resources);
    rejects([&] { cyclic.validate_target("ferrium_part"); }, "Infeasible production balance");
    facilities = fixture.facilities;
    facilities[0].recipes[0].unsupported_reasons = {"environment requirement"};
    RecipeGraph unsupported(facilities, fixture.region.resources);
    rejects([&] { unsupported.validate_target("ferrium_part"); }, "environment requirement");
    facilities = fixture.facilities;
    facilities[0].inputs[0].type = TransportMedium::Pipe;
    RecipeGraph pipes(facilities, fixture.region.resources);
    rejects([&] { pipes.validate_target("ferrium_part"); }, "pipe transport");
}
void invalid_datasets(const std::filesystem::path &root)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary =
        std::filesystem::temp_directory_path() / ("aic-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(temporary);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{temporary};
    for (const auto &entry : std::filesystem::directory_iterator(root / "data/test"))
        std::filesystem::copy_file(entry.path(), temporary / entry.path().filename());
    nlohmann::json original;
    {
        std::ifstream input(temporary / "region.json");
        input >> original;
    }
    auto invalid = original;
    invalid["resources"].push_back(invalid["resources"][0]);
    {
        std::ofstream output(temporary / "region.json");
        output << invalid;
    }
    rejects([&] { Dataset::load(temporary); }, "duplicate resource");
    {
        std::ofstream output(temporary / "region.json");
        output << "{";
    }
    rejects([&] { Dataset::load(temporary); }, "region.json");
    invalid = original;
    invalid["storage"] = -1;
    {
        std::ofstream output(temporary / "region.json");
        output << invalid;
    }
    rejects([&] { Dataset::load(temporary); }, "storage");
    {
        std::ofstream output(temporary / "region.json");
        output << original;
    }
    nlohmann::json facilities;
    {
        std::ifstream input(temporary / "facilities.json");
        input >> facilities;
    }
    facilities[0]["inputs"][0]["fx"] = 1;
    facilities[0]["inputs"][0]["fy"] = 0;
    {
        std::ofstream output(temporary / "facilities.json");
        output << facilities;
    }
    rejects([&] { Dataset::load(temporary); }, "faces into footprint");
    // The catalog market controls products and prices without a products.json file.
    nlohmann::json market_region;
    {
        std::ifstream input(root / "data/valley_iv/region.json");
        input >> market_region;
    }
    market_region["catalog"] = (root / "data/catalog.json").string();
    market_region["settlement_levels"] = {
        {"stm_tundra_1", 1}, {"stm_tundra_2", 0}, {"stm_tundra_3", 0}};
    {
        std::ofstream output(temporary / "region.json");
        output << market_region;
    }
    const auto locked = Dataset::load(temporary);
    check(locked.products.size() < 14 && !locked.products.empty(),
          "Respect configured outpost levels");
    for (const auto &product : locked.products)
        check(product.name != "hc_valley_battery",
              "A locked high-tier product must not enter the objective");
    market_region.erase("settlement_levels");
    market_region["market_region"] = "domain_2";
    {
        std::ofstream output(temporary / "region.json");
        output << market_region;
    }
    const auto permanent_wuling = Dataset::load(temporary);
    check(permanent_wuling.products.size() == 12,
          "Wuling has twelve permanent sale products in this snapshot");
    market_region["active_activities"] = {"activity_limited_formula_1",
                                          "activity_limited_formula_2"};
    {
        std::ofstream output(temporary / "region.json");
        output << market_region;
    }
    const auto event_wuling = Dataset::load(temporary);
    check(event_wuling.products.size() == 16,
          "Preserve and explicitly enable the four event sale products");
}

void production_flows(const Dataset &data)
{
    RecipeGraph graph(data.facilities, data.region.resources, data.automatic_recipes);
    for (const auto &product : data.products)
    {
        const auto plan = graph.plan_target(product.name);
        std::map<std::string, double> net = plan.external_rates;
        for (const auto &step : plan.steps)
        {
            check(step.cycles_per_second <= step.instances / step.recipe->time + 1e-8,
                  "Machine capacity must cover recipe flow");
            for (const auto &ingredient : step.recipe->ingredients)
                net[ingredient.item] -= step.cycles_per_second * ingredient.amount;
            for (const auto &output : step.recipe->products)
                net[output.item] += step.cycles_per_second * output.amount;
        }
        for (const auto &[item, rate] : net)
            check(std::abs(rate - (item == product.name ? plan.target_rate : 0)) < 1e-7,
                  "Steady-state conservation failed for " + product.name + ": " + item);
        if (product.name.find("buck_capsule") != std::string::npos ||
            product.name.find("canned_citrome") != std::string::npos)
            check(!plan.startup_items.empty(),
                  "Crop recipes must expose the initial-stock requirement");
        const auto network = build_factory_network(
            plan, product.name, graph.facility("protocol_stash"), graph.facility("unloader"));
        for (const auto &entity : network.entities)
            if (entity.loop_role != SeedLoopRole::None)
            {
                check(entity.loop_partner >= 0,
                      "Every seed-loop machine needs a dedicated partner");
                if (entity.loop_role == SeedLoopRole::Picker)
                {
                    double returned = 0, exported = 0;
                    for (const auto &stream : network.streams)
                        if (stream.src_entity == entity.id)
                        {
                            if (stream.dst_entity == entity.loop_partner)
                                returned += stream.rate;
                            else
                                exported += stream.rate;
                        }
                    check(std::abs(returned - exported) < 1e-8 && returned > 0,
                          "Return one seed to the paired planter and expose one spare seed per "
                          "cycle");
                }
                for (const auto &stream : network.streams)
                {
                    if (stream.dst_entity == entity.id)
                        check(stream.src_entity == entity.loop_partner,
                              "Seed module must replenish itself without external inputs");
                    if (entity.loop_role == SeedLoopRole::Planter && stream.src_entity == entity.id)
                        check(stream.dst_entity == entity.loop_partner,
                              "The paired flower must return to its seed picker");
                }
            }
        std::set<std::pair<int, int>> input_ports, output_ports;
        std::map<std::pair<int, std::string>, double> incoming, outgoing;
        for (const auto &stream : network.streams)
        {
            check(stream.rate > 0 && stream.rate <= 0.5 + 1e-8,
                  "Every stream must fit belt capacity");
            check(input_ports.insert({stream.dst_entity, stream.dst_port_idx}).second,
                  "Input port reused by two belts");
            check(output_ports.insert({stream.src_entity, stream.src_port_idx}).second,
                  "Output port reused by two belts");
            incoming[{stream.dst_entity, stream.item}] += stream.rate;
            outgoing[{stream.src_entity, stream.item}] += stream.rate;
        }
        for (const auto &entity : network.entities)
        {
            for (const auto &[item, rate] : entity.input_rates)
                check(std::abs(incoming[{entity.id, item}] - rate) < 1e-7,
                      "Unsatisfied input flow for " + product.name);
            for (const auto &[item, rate] : entity.output_rates)
                check(std::abs(outgoing[{entity.id, item}] - rate) < 1e-7,
                      "Unbalanced output flow for " + product.name);
        }
        if (product.name == "buck_capsule_b")
        {
            check(plan.startup_stock.at("buckflower") == 1 &&
                      !plan.startup_stock.count("buckflower_seed"),
                  "Initialize the closed seed module once with a flower, not externally supplied "
                  "seeds");
        }
    }
    // Keep a high-throughput case independent of the crop candidate scale.
    Recipe fast;
    fast.name = "parallel_belt_fixture";
    fast.time = 1;
    fast.products.push_back({"parallel_output", 1});
    ProductionPlan parallel_plan;
    parallel_plan.target_rate = 1;
    parallel_plan.steps.push_back(
        {&graph.facility("refining_unit"), &fast, 1, 1, {}, SeedLoopRole::None});
    const auto parallel_network =
        build_factory_network(parallel_plan, "parallel_output", graph.facility("protocol_stash"),
                              graph.facility("unloader"));
    check(parallel_network.streams.size() == 2, "One-item/second output needs two parallel belts");
    check(parallel_network.streams[0].src_port_idx != parallel_network.streams[1].src_port_idx &&
              parallel_network.streams[0].dst_port_idx !=
                  parallel_network.streams[1].dst_port_idx &&
              std::abs(parallel_network.streams[0].rate - 0.5) < 1e-8 &&
              std::abs(parallel_network.streams[1].rate - 0.5) < 1e-8,
          "Parallel belts must use distinct ports and respect capacity");
}

void standalone_seed_loop(const Dataset &data)
{
    RecipeGraph graph(data.facilities, data.region.resources, true);
    const auto plan = graph.plan_target("sandleaf_seed");
    check(plan.external_rates.empty(), "Seed loop must not consume depot resources");
    auto supplied = data.region.resources;
    supplied.push_back({"sandleaf", 100});
    RecipeGraph with_external_flowers(data.facilities, supplied, true);
    check(with_external_flowers.plan_target("sandleaf_seed").external_rates.empty(),
          "Seed-loop exception must remain closed even when flowers are listed as available "
          "external supplies");
    check(plan.steps.size() == 2 && plan.steps[0].instances == 1 && plan.steps[1].instances == 1,
          "Standalone seed module uses exactly one picker and one planter");
    check(std::abs(plan.target_rate - 0.25) < 1e-9,
          "One spare seed per four-second closed circuit with one initial flower");
    check(plan.startup_stock.at("sandleaf") == 1, "One initial Sandleaf flower starts the loop");
    const auto network = build_factory_network(
        plan, "sandleaf_seed", graph.facility("protocol_stash"), graph.facility("unloader"));
    check(network.total_unloaders == 0 && network.entities.size() == 3,
          "Seed export uses two production machines plus a stash, and no depot unloader");
    int export_belts = 0;
    for (const auto &stream : network.streams)
        if (stream.dst_entity == network.stash_entity_id)
            ++export_belts;
    check(export_belts == 1, "One spare-seed export connection");
    BlueprintOptions options;
    options.automatic_recipes = true;
    BlueprintSolver solver(data.facilities, data.region.resources, data.areas, options);
    Product target;
    target.name = "sandleaf_seed";
    BlueprintLayout layout;
    check(solver.solve(target, layout), "Standalone seed loop must place and route");
    check(layout.depot_length == 0 && target.mineral_consumption.empty(),
          "No depot frontage or ongoing external material supply");
}

void plant_templates(const Dataset &data)
{
    RecipeGraph graph(data.facilities, data.region.resources, true);
    const auto loops = graph.seed_loops();
    check(loops.size() == 4, "Four Valley IV plants have closed production loops");
    BlueprintOptions options;
    options.automatic_recipes = true;
    auto exact_area = data.areas;
    exact_area.front().pac_width = 15;
    exact_area.front().pac_height = 9;
    BlueprintSolver solver(data.facilities, data.region.resources, exact_area, options);
    for (const auto &loop : loops)
    {
        const auto plan = graph.plan_target(loop.flower);
        check(plan.external_rates.empty() && plan.startup_stock.at(loop.flower) == 1,
              "Plant module needs one initial flower and no recurring external supply");
        Product target;
        target.name = loop.flower;
        BlueprintLayout layout;
        check(solver.solve(target, layout), "Every Valley IV plant must use the fixed template");
        check(layout.bounding_width == 15 && layout.bounding_height == 9 &&
                  layout.depot_length == 0,
              "Plant template footprint is 15x9 with no depot");
        check(layout.entities.size() == 5 && target.factory_facilities.at("planting_unit") == 2 &&
                  target.factory_facilities.at("seed_picking_unit") == 1 &&
                  target.factory_facilities.at("power_diffuser_1") == 1,
              "Two planters, one picker, one stash, and one electric pylon");
        check(std::abs(layout.output_per_minute - 15) < 1e-8,
              "One-flower startup supports fifteen plants per minute");
        std::set<std::pair<int, int>> occupied;
        for (const auto &entity : layout.entities)
        {
            if (entity.type == "planting_unit")
                check((entity.x == 0 || entity.x == 10) && entity.y == 3 && entity.rotation == 180,
                      "Planters flank the picker");
            if (entity.type == "seed_picking_unit")
                check(entity.x == 5 && entity.y == 3 && entity.rotation == 0, "Central picker");
            if (entity.type == "protocol_stash")
                check(entity.x == 6 && entity.y == 0 && entity.rotation == 270,
                      "Stash below picker");
            if (entity.type == "power_diffuser_1")
                check(entity.x == 9 && entity.y == 0, "Pylon beside stash");
            for (int x = entity.x; x < entity.x + entity.width; ++x)
                for (int y = entity.y; y < entity.y + entity.height; ++y)
                    check(occupied.insert({x, y}).second, "Template buildings must not overlap");
        }
        const std::map<std::pair<int, int>, std::string> expected{
            {{4, 8}, loop.seed},   {{5, 8}, loop.seed},   {{9, 8}, loop.seed},
            {{10, 8}, loop.seed},  {{4, 2}, loop.flower}, {{5, 2}, loop.flower},
            {{9, 2}, loop.flower}, {{10, 2}, loop.flower}};
        check(layout.is_conveyor_feasible && layout.conveyor_items == expected,
              "Exact short internal loops and plant export belt");
        for (const auto &[cell, item] : layout.conveyor_items)
            check(!occupied.count(cell), "Belts must stay clear of the pylon and other buildings");
    }
}

void regional_model()
{
    Product product;
    product.name = "part";
    product.value = 0.5;
    product.production_time = 2;
    product.mineral_consumption["ore"] = 1.5;
    product.factory_width = 6;
    product.factory_height = 6;
    product.factory_depot = 3;
    std::vector<Product> products{product};
    std::vector<Mineral> resources{{"ore", 4}};
    Area area;
    area.name = "test";
    area.pac_width = 12;
    area.pac_height = 12;
    area.pac_depot_width = 12;
    area.static_depots.push_back({0, 12, 11, 12});
    std::vector<Area> areas{area};
    std::vector<Fuel> fuels;
    std::map<std::string, double> powers;
    Region region;
    region.storage = 80000;
    region.base_power = 200;
    BlueprintLayout rectangle;
    rectangle.bounding_width = 6;
    rectangle.bounding_height = 6;
    rectangle.depot_length = 3;
    Solver solver(products, resources, areas, fuels, powers, region);
    solver.set_render_layouts(false);
    solver.set_blueprint_layouts({{"part", rectangle}});
    check(solver.solve(), "Regional fixture must solve");
    check(solver.status() == "OPTIMAL", "Expected proven optimal master result");
    check(
        std::abs(solver.objective_value() - 1) < 1e-9,
        "Fractional resource consumption must bound output to two units; retain fractional price");
    check(solver.solve() && std::abs(solver.objective_value() - 1) < 1e-9,
          "Repeated solving must start from a fresh model");
    // A zero-slot area must force factory and production counts to zero.
    areas[0].static_depots.clear();
    areas[0].pac_depot_width = 0;
    areas[0].area_facilities["unused_infrastructure"] = 0;
    Solver blocked(products, resources, areas, fuels, powers, region);
    blocked.set_render_layouts(false);
    blocked.set_blueprint_layouts({{"part", rectangle}});
    check(blocked.solve() && blocked.objective_value() == 0, "No depot must mean no production");
    areas[0].area_facilities["infrastructure"] = 1;
    powers["infrastructure"] = 300;
    Solver infeasible(products, resources, areas, fuels, powers, region);
    infeasible.set_render_layouts(false);
    infeasible.set_blueprint_layouts({{"part", rectangle}});
    check(!infeasible.solve() && infeasible.status() == "INFEASIBLE",
          "Unpowered static infrastructure must report infeasibility");
    // Storage bounds net fuel output, not fuel production before its own power use.
    products[0].name = "battery";
    products[0].value = 1;
    products[0].mineral_consumption["ore"] = 1;
    products[0].factory_facilities["machine"] = 1;
    resources[0].limit = 100;
    powers = {{"machine", 1}};
    areas[0].area_facilities.clear();
    areas[0].static_depots = {{0, 12, 11, 12}};
    areas[0].pac_depot_width = 12;
    fuels = {{"battery", 10, 60}};
    region.base_power = 0;
    region.storage = 2880; // one net unit per minute over the 48-hour horizon
    Solver self_powered(products, resources, areas, fuels, powers, region);
    self_powered.set_render_layouts(false);
    self_powered.set_blueprint_layouts({{"battery", rectangle}});
    check(self_powered.solve() && self_powered.objective_value() == 1,
          "Fuel production must include both the unit sold and the unit burned");
}
void blueprint(const Dataset &fixture)
{
    BlueprintSolver solver(fixture.facilities, fixture.region.resources, fixture.areas);
    Product target;
    target.name = "sc_valley_battery";
    target.value = 30;
    BlueprintLayout layout;
    check(solver.solve(target, layout), "Fixed battery chain must route");
    check(solver.status() == BlueprintStatus::Success && layout.is_conveyor_feasible,
          "Successful blueprint must be marked routed");
    check(target.production_time == 10 && target.mineral_consumption.at("ferrium_ore") == 10 &&
              target.mineral_consumption.at("originium_ore") == 15,
          "Battery recipe resource accounting");
    check(target.factory_facilities.at("refining_unit") == 2 &&
              target.factory_facilities.at("shredding_unit") == 3 &&
              target.factory_facilities.at("fitting_unit") == 2,
          "Battery facility sizing");
    std::set<std::pair<int, int>> occupied;
    for (const auto &entity : layout.entities)
        for (int x = entity.x; x < entity.x + entity.width; ++x)
            for (int y = entity.y; y < entity.y + entity.height; ++y)
            {
                check(x >= 0 && y >= 0 && x < layout.bounding_width && y < layout.bounding_height,
                      "Facility must fit blueprint bounds");
                check(occupied.insert({x, y}).second, "Facilities must not overlap");
            }
    BlueprintSolver timed_out(fixture.facilities, fixture.region.resources, fixture.areas,
                              {0.0, 1});
    Product timeout_target = target;
    BlueprintLayout timeout_layout;
    check(!timed_out.solve(timeout_target, timeout_layout) &&
              timed_out.status() == BlueprintStatus::Timeout,
          "A zero-time placement search must report timeout, not infeasibility");
    check(!layout.conveyor_cells.empty(), "Expected routed conveyors");
    for (const auto &[cell, mask] : layout.conveyor_cells)
    {
        check(!occupied.count(cell), "Conveyors must not cross facilities");
        check(cell.first >= 0 && cell.second >= 0 && cell.first < layout.bounding_width &&
                  cell.second < layout.bounding_height,
              "Conveyor outside blueprint");
    }
}
int main(int argc, char **argv)
{
    try
    {
        check(argc == 2, "Expected repository path");
        log_level = LogLevel::Warning;
        const auto root = std::filesystem::path(argv[1]);
        const auto fixture = Dataset::load(root / "data/test");
        command_line();
        layout_geometry();
        dependencies(fixture);
        invalid_datasets(root);
        const auto imported = Dataset::load(root / "data/valley_iv");
        RecipeGraph graph(imported.facilities, imported.region.resources,
                          imported.automatic_recipes);
        graph.validate_target("sc_valley_battery");
        check(imported.products.size() == 14,
              "All Valley IV sale products must be loaded automatically");
        std::map<std::string, double> prices;
        for (const auto &product : imported.products)
            prices[product.name] = product.value;
        check(prices.at("hc_valley_battery") == 70 && prices.at("amethyst_bottle") == 2 &&
                  prices.at("steel_part") == 3,
              "Imported regional prices must match source offers");
        const auto refinery =
            std::find_if(imported.facilities.begin(), imported.facilities.end(),
                         [](const Facility &facility) { return facility.name == "refining_unit"; });
        check(refinery != imported.facilities.end() && !refinery->inputs.empty(),
              "Imported refinery ports");
        production_flows(imported);
        standalone_seed_loop(imported);
        plant_templates(imported);
        regional_model();
        blueprint(fixture);
        std::cout << "Planner regression checks passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
