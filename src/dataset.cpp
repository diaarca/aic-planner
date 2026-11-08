#include "dataset.hpp"
#include "json_io.hpp"
#include "logging.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace aic
{
namespace
{
using nlohmann::json;
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::invalid_argument(message);
}
void number(double value, const std::string &label, bool allow_zero = false)
{
    require(std::isfinite(value) && (allow_zero ? value >= 0 : value > 0) && value <= 1e9,
            label + ": expected finite " + (allow_zero ? "nonnegative" : "positive") +
                " number <= 1e9");
}
void unique(std::set<std::string> &seen, const std::string &value, const std::string &kind)
{
    require(!value.empty() && seen.insert(value).second,
            "Empty or duplicate " + kind + ": " + value);
}
void load_market(const json &market, const json &fuels, Dataset &data)
{
    auto &region = data.region;
    region.currency = market.at("currency_id").get<std::string>();
    const std::set<std::string> activities(region.active_activities.begin(),
                                           region.active_activities.end());
    const std::set<std::string> time_ids(region.active_time_ids.begin(),
                                         region.active_time_ids.end());
    auto available = [&](const json &record)
    {
        const auto activity = record.value("activity_id", "");
        const auto time = record.value("time_id", "");
        return (activity.empty() || activities.count(activity)) &&
               (time.empty() || time_ids.count(time));
    };
    std::map<std::string, Product> targets;
    std::set<std::string> outpost_ids;
    for (const auto &outpost : market.at("outposts"))
    {
        const auto id = outpost.at("id").get<std::string>();
        outpost_ids.insert(id);
        const int requested = region.settlement_levels.count(id)
                                  ? region.settlement_levels.at(id)
                                  : outpost.at("levels").back().at("level").get<int>();
        require(requested >= 0, "Outpost level cannot be negative: " + id);
        const json *level = nullptr;
        for (const auto &candidate : outpost.at("levels"))
            if (candidate.at("level") == requested)
                level = &candidate;
        if (requested == 0)
            continue; // explicitly locked outpost
        require(level != nullptr, "Unknown outpost level: " + id);
        if (!available(*level))
            continue;
        for (const auto &offer : level->at("offers"))
        {
            if (!available(offer))
                continue;
            const auto name = offer.at("product").get<std::string>();
            auto &target = targets[name];
            target.name = name;
            target.value = std::max(target.value, offer.at("value").get<double>());
        }
    }
    for (const auto &[id, level] : region.settlement_levels)
        require(outpost_ids.count(id), "Unknown outpost in market configuration: " + id);
    for (const auto &[name, product] : targets)
        data.products.push_back(product);
    for (const auto &fuel : fuels)
        if (targets.count(fuel.at("fuel").get<std::string>()))
            data.fuels.emplace_back(fuel.at("fuel"), fuel.at("power"), fuel.at("duration"));
    log(LogLevel::Info, "Loaded ", data.products.size(), " sellable products for ",
        market.at("name").get<std::string>(), " (", region.currency,
        "); prices before outpost cash limits and liaison bonuses");
}
void load_catalog(const std::filesystem::path &path, Dataset &data)
{
    auto &region = data.region;
    const auto catalog = read_json_file(path);
    require(catalog.at("schema_version") == 2,
            "Unsupported catalog schema; refresh the game catalog");
    const json *market = nullptr;
    if (!region.market_region.empty())
    {
        for (const auto &candidate : catalog.at("regions"))
            if (candidate.at("id") == region.market_region)
            {
                market = &candidate;
                break;
            }
        require(market != nullptr, "Unknown catalog market region: " + region.market_region);
        load_market(*market, catalog.at("fuels"), data);
    }
    // No allowlist means all catalog facilities/recipes are candidates. Region modes
    // and explicit facility placement domains determine availability.
    data.automatic_recipes = region.enabled_recipes.empty();
    if (region.enabled_facilities.empty())
        for (const auto &facility : catalog.at("facilities"))
        {
            const auto &domains = facility.at("place_domains");
            if (!market || domains.empty() ||
                std::find(domains.begin(), domains.end(), region.market_region) != domains.end())
                region.enabled_facilities.push_back(facility.at("id"));
        }
    if (region.enabled_recipes.empty())
        for (const auto &recipe : catalog.at("recipes"))
        {
            const auto machine = recipe.at("facility_id").get<std::string>();
            if (std::find(region.enabled_facilities.begin(), region.enabled_facilities.end(),
                          machine) == region.enabled_facilities.end())
                continue;
            bool mode_allowed = !market;
            if (market)
                for (const auto &mode : recipe.at("modes"))
                    if (std::find(market->at("machine_modes").begin(),
                                  market->at("machine_modes").end(),
                                  mode) != market->at("machine_modes").end())
                        mode_allowed = true;
            if (mode_allowed)
                region.enabled_recipes.push_back(recipe.at("id"));
        }
    std::set<std::string> facilities, recipes;
    for (const auto &id : region.enabled_facilities)
        unique(facilities, id, "enabled facility");
    for (const auto &id : region.enabled_recipes)
        unique(recipes, id, "enabled recipe");
    std::map<std::string, json> selected;
    for (auto facility : catalog.at("facilities"))
    {
        const auto id = facility.at("id").get<std::string>();
        if (!facilities.count(id))
            continue;
        facility["recipes"] = json::array();
        require(selected.emplace(id, facility).second, "Duplicate catalog facility: " + id);
    }
    require(selected.size() == facilities.size(), "Unknown enabled facility ID");
    std::set<std::string> found_recipes;
    for (const auto &recipe : catalog.at("recipes"))
    {
        const auto id = recipe.at("id").get<std::string>();
        if (!recipes.count(id))
            continue;
        unique(found_recipes, id, "catalog recipe");
        const auto machine = recipe.at("facility_id").get<std::string>();
        require(selected.count(machine), "Recipe " + id + " uses a disabled facility: " + machine);
        selected.at(machine)["recipes"].push_back(recipe);
    }
    require(found_recipes == recipes, "Unknown enabled recipe ID");
    json output = json::array();
    for (auto &[id, facility] : selected)
    {
        // Solid-only recipes must not reserve unused pipe doorsteps in placement.
        // Full bindings remain in the catalog for later pipe support.
        bool solid_only = true;
        for (const auto &recipe : facility["recipes"])
        {
            const auto &bindings = recipe.at("port_bindings");
            if (!bindings.at("pipeIngredientBufferBinding").empty() ||
                !bindings.at("pipeOutcomeBufferBinding").empty())
                solid_only = false;
        }
        if (solid_only)
            for (const auto *side : {"inputs", "outputs"})
            {
                auto &ports = facility[side];
                ports.erase(std::remove_if(ports.begin(), ports.end(),
                                           [](const json &port)
                                           { return port.at("type") == "pipe"; }),
                            ports.end());
            }
        output.push_back(facility);
    }
    // Regional supplies may use upstream IDs or their compatibility names.
    for (auto &resource : region.resources)
        for (const auto &item : catalog.at("items"))
            if (item.at("id") == resource.name)
            {
                resource.name = item.at("name").get<std::string>();
                break;
            }
    log(LogLevel::Info, "Loaded catalog ", catalog.at("source").at("version").get<std::string>());
    data.facilities = Facility::from_json(output);
}
void validate(Dataset &data)
{
    number(data.region.base_power, "base_power", true);
    number(data.region.storage, "storage");
    require(std::floor(data.region.base_power) == data.region.base_power,
            "base_power must be integer power units");
    require(!data.products.empty() && !data.facilities.empty() && !data.areas.empty(),
            "Empty essential dataset");
    std::set<std::string> resources, facilities, recipes, products, areas, fuels;
    for (const auto &resource : data.region.resources)
    {
        unique(resources, resource.name, "resource");
        number(resource.limit, resource.name + " supply", true);
    }
    for (const auto &facility : data.facilities)
    {
        unique(facilities, facility.name, "facility");
        number(facility.power, facility.name + " power", true);
        require(std::floor(facility.power) == facility.power,
                "Facility power must be integer units");
        require(facility.width > 0 && facility.height > 0, "Invalid footprint: " + facility.name);
        for (const auto *ports : {&facility.inputs, &facility.outputs})
            for (const auto &port : *ports)
            {
                require(port.x >= 0 && port.x < facility.width && port.y >= 0 &&
                            port.y < facility.height,
                        "Port outside footprint: " + facility.name);
                require(std::abs(port.fx) + std::abs(port.fy) == 1,
                        "Invalid port direction: " + facility.name);
                require(port.x + port.fx < 0 || port.x + port.fx >= facility.width ||
                            port.y + port.fy < 0 || port.y + port.fy >= facility.height,
                        "Port faces into footprint: " + facility.name);
            }
        for (const auto &recipe : facility.recipes)
        {
            unique(recipes, recipe.name, "recipe");
            number(recipe.time, recipe.name + " time");
            require(!recipe.products.empty() && !recipe.ingredients.empty(),
                    "Empty recipe: " + recipe.name);
            for (const auto &item : recipe.ingredients)
            {
                require(!item.item.empty(), "Empty ingredient: " + recipe.name);
                number(item.amount, recipe.name + " ingredient quantity");
            }
            for (const auto &item : recipe.products)
            {
                require(!item.item.empty(), "Empty output: " + recipe.name);
                number(item.amount, recipe.name + " output quantity");
            }
        }
    }
    require(facilities.count("protocol_stash") && facilities.count("unloader"),
            "protocol_stash and unloader infrastructure facilities are required");
    for (const auto &product : data.products)
    {
        unique(products, product.name, "product");
        number(product.value, product.name + " value", true);
        require(std::abs(product.value * 1000 - std::round(product.value * 1000)) < 1e-6,
                "Product values support at most three decimal places");
    }
    for (const auto &area : data.areas)
    {
        unique(areas, area.name, "area");
        number(area.pac_width, area.name + " width", true);
        number(area.pac_height, area.name + " height", true);
        require(std::floor(area.pac_width) == area.pac_width &&
                    std::floor(area.pac_height) == area.pac_height,
                "Area dimensions must be integer tiles");
        for (const auto &[facility, count] : area.area_facilities)
        {
            number(count, "Infrastructure count", true);
            require(std::floor(count) == count, "Infrastructure counts must be integers");
            require(count == 0 || facilities.count(facility),
                    "Unknown infrastructure facility: " + facility);
        }
        std::set<int> sides;
        for (const auto &depot : area.static_depots)
        {
            const int width = area.pac_width, height = area.pac_height;
            require(depot.x1 >= 0 && depot.x2 >= 0 && depot.x1 <= width && depot.x2 <= width &&
                        depot.y1 >= 0 && depot.y2 >= 0 && depot.y1 <= height && depot.y2 <= height,
                    "Depot outside area: " + area.name);
            int side = -1;
            if (depot.y1 == height && depot.y2 == height)
                side = 0;
            else if (depot.x1 == width && depot.x2 == width)
                side = 1;
            else if (depot.y1 == 0 && depot.y2 == 0)
                side = 2;
            else if (depot.x1 == 0 && depot.x2 == 0)
                side = 3;
            require(side >= 0, "Depot must be on an area boundary: " + area.name);
            require(sides.insert(side).second,
                    "Multiple depot segments on one edge are unsupported: " + area.name);
        }
    }
    for (const auto &fuel : data.fuels)
    {
        unique(fuels, fuel.name, "fuel");
        number(fuel.duration, fuel.name + " duration");
        number(fuel.power, fuel.name + " power");
        require(std::floor(fuel.power) == fuel.power, "Fuel power must be integer units");
        require(std::floor(fuel.duration) == fuel.duration && fuel.duration <= 3600,
                "Fuel duration must be integer seconds <= 3600");
    }
}
} // namespace

Dataset Dataset::load(const std::filesystem::path &folder)
{
    Dataset data;
    data.region = Region::read_json((folder / "region.json").string());
    if (data.region.market_region.empty())
        data.products = Product::read_json((folder / "products.json").string());
    data.areas = Area::read_json((folder / "areas.json").string());
    if (data.region.market_region.empty())
        data.fuels = Fuel::read_json((folder / "fuels.json").string());
    if (!data.region.catalog.empty())
        load_catalog(folder / data.region.catalog, data);
    else
    {
        require(data.region.market_region.empty(), "market_region requires a catalog");
        data.automatic_recipes = data.region.automatic_recipes;
        data.facilities = Facility::read_json((folder / "facilities.json").string());
        if (!data.region.enabled_facilities.empty() || !data.region.enabled_recipes.empty())
            throw std::invalid_argument(
                "Enabled IDs require a catalog; local fixtures include exactly their test recipes");
    }
    for (auto &area : data.areas)
    {
        std::map<std::string, double> infrastructure;
        for (const auto &[identity, count] : area.area_facilities)
        {
            std::string name = identity;
            for (const auto &facility : data.facilities)
                if (facility.id == identity)
                {
                    name = facility.name;
                    break;
                }
            require(infrastructure.emplace(name, count).second,
                    "Duplicate infrastructure alias: " + identity);
        }
        area.area_facilities = std::move(infrastructure);
    }
    validate(data);
    return data;
}
} // namespace aic
