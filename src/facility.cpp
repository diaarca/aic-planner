#include "facility.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace aic
{

MaterialPhase material_phase(const std::string &name)
{
    if (name == "solid")
        return MaterialPhase::Solid;
    if (name == "liquid")
        return MaterialPhase::Liquid;
    if (name == "gas")
        return MaterialPhase::Gas;
    throw std::invalid_argument("Unknown material phase: " + name);
}

double transport_capacity(MaterialPhase phase)
{
    return phase == MaterialPhase::Solid ? 1.0 / solid_belt_seconds_per_item
                                         : pipe_units_per_second;
}

static int tile(const nlohmann::json &item, const char *field)
{
    const auto &value = item.at(field);
    if (!value.is_number_integer() || value.get<double>() < -1000000 ||
        value.get<double>() > 1000000)
        throw std::invalid_argument(std::string("Expected integer tile coordinate/dimension: ") +
                                    field);
    return value.get<int>();
}

static void determine_port_facing(Port &p, int w, int h)
{
    if (p.y == 0)
    {
        p.fx = 0;
        p.fy = -1;
    }
    else if (p.y == h - 1)
    {
        p.fx = 0;
        p.fy = 1;
    }
    else if (p.x == 0)
    {
        p.fx = -1;
        p.fy = 0;
    }
    else if (p.x == w - 1)
    {
        p.fx = 1;
        p.fy = 0;
    }
    else
    {
        p.fx = 0;
        p.fy = -1;
    }
}

std::vector<Facility> Facility::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    return from_json(j);
}

std::vector<Facility> Facility::from_json(const nlohmann::json &j)
{
    if (!j.is_array())
        throw std::invalid_argument("Facilities must be an array");
    std::vector<Facility> facilities;
    for (const auto &item : j)
    {
        Facility f;
        f.id = item.value("id", item.value("name", ""));
        f.name = item.value("name", "");
        f.power = item.value("power", 0.0);
        f.width = tile(item, "width");
        f.height = tile(item, "height");
        f.needs_power = item.contains("properties")
                            ? item.at("properties").value("needPower", f.power > 0)
                            : f.power > 0;
        if (item.contains("specializations") &&
            item.at("specializations").contains("FactoryPowerPoleTable"))
        {
            const auto &pole = item.at("specializations").at("FactoryPowerPoleTable");
            if (pole.contains("rangeExtend"))
            {
                const auto &range = pole.at("rangeExtend");
                const int x = tile(range, "x"), y = tile(range, "z");
                if (x < 0 || y < 0)
                    throw std::invalid_argument("Power range extension must be nonnegative");
                f.power_range_extension = {x, y};
            }
        }

        auto read_ports = [&](const char *side)
        {
            std::vector<Port> ports;
            if (!item.contains(side))
                return ports;
            for (const auto &value : item.at(side))
            {
                Port port;
                port.x = tile(value, "x");
                port.y = tile(value, "y");
                port.source_index = value.value("index", static_cast<int>(ports.size()));
                const auto medium = value.value("type", "solid");
                if (medium != "solid" && medium != "product" && medium != "liquid" &&
                    medium != "pipe")
                    throw std::invalid_argument("Unknown transport medium: " + medium);
                port.type = (medium == "liquid" || medium == "pipe") ? TransportMedium::Pipe
                                                                     : TransportMedium::Solid;
                if (value.contains("fx") != value.contains("fy"))
                    throw std::invalid_argument("Port needs both fx and fy");
                if (value.contains("fx"))
                {
                    port.fx = tile(value, "fx");
                    port.fy = tile(value, "fy");
                }
                else
                    determine_port_facing(port, f.width, f.height);
                ports.push_back(port);
            }
            return ports;
        };
        f.inputs = read_ports("inputs");
        f.outputs = read_ports("outputs");
        if (item.contains("activation"))
        {
            const auto &activation = item.at("activation");
            f.activation = ActivationFeed{
                activation.at("item"), material_phase(activation.at("transport")),
                activation.at("per_minute").get<double>() / 60, tile(activation, "port")};
        }
        for (const auto &[name, rate] :
             item.value("disposal_rates", std::map<std::string, double>{}))
            f.disposal_rates.emplace(name, rate / 60);

        if (item.contains("recipes"))
        {
            for (const auto &rec : item["recipes"])
            {
                Recipe r;
                r.has_port_bindings = rec.contains("port_bindings");
                r.id = rec.value("id", rec.value("name", ""));
                r.unsupported_reasons =
                    rec.value("unsupported_reasons", std::vector<std::string>{});
                r.name = rec.value("name", "");
                r.time = rec.value("time", 0.0);
                r.environment = rec.value("environment", 0);

                const auto read_materials = [&](const char *key, const char *groups_key,
                                                const char *solid_key, const char *pipe_key,
                                                auto &materials)
                {
                    if (!rec.contains(key))
                        return;
                    const auto read = [&](const auto &value, size_t group)
                    {
                        typename std::decay_t<decltype(materials)>::value_type entry;
                        entry.item = value.value("item", "");
                        entry.amount = value.value("amount", 0.0);
                        entry.phase = material_phase(value.value("transport", "solid"));
                        if (r.has_port_bindings)
                        {
                            const bool pipe = entry.phase != MaterialPhase::Solid;
                            const auto &bindings =
                                rec.at("port_bindings").at(pipe ? pipe_key : solid_key);
                            const size_t binding = bindings.size() == 1 ? 0 : group;
                            if (binding < bindings.size())
                                entry.ports = bindings.at(binding)
                                                  .at("bindingPortIndices")
                                                  .template get<std::vector<int>>();
                        }
                        materials.push_back(std::move(entry));
                    };
                    if (!rec.contains(groups_key))
                    {
                        for (const auto &value : rec.at(key))
                            read(value, 0);
                        return;
                    }
                    const auto &groups = rec.at(groups_key);
                    for (size_t group = 0; group < groups.size(); ++group)
                        for (const auto &value : groups.at(group))
                            read(value, group);
                };
                read_materials("ingredients", "ingredient_groups", "ingredientBufferBinding",
                               "pipeIngredientBufferBinding", r.ingredients);
                read_materials("products", "product_groups", "outcomeBufferBinding",
                               "pipeOutcomeBufferBinding", r.products);
                f.recipes.push_back(r);
            }
        }
        facilities.push_back(f);
    }
    return facilities;
}

std::ostream &operator<<(std::ostream &os, const Facility &f)
{
    os << "Facility: " << std::left << std::setw(20) << f.name << " | Power: " << f.power;
    return os;
}

void Facility::print_table(const std::map<std::string, double> &facilities)
{
    std::cout << "\n--- Facilities Table ---\n";
    std::cout << std::left << std::setw(25) << "Facility"
              << " | " << std::setw(10) << "Power"
              << "\n";
    std::cout << std::string(40, '-') << "\n";

    for (const auto &[name, power] : facilities)
    {
        std::cout << std::left << std::setw(25) << name << " | " << std::setw(10) << power << "\n";
    }
}

double facility_power_consumption(const std::map<std::string, double> &counts,
                                  const std::map<std::string, double> &power)
{
    double total = 0;
    for (const auto &[name, count] : counts)
    {
        if (count == 0)
            continue;
        const auto found = power.find(name);
        if (found == power.end())
            throw std::invalid_argument("Missing power specification for facility: " + name);
        total += count * found->second;
    }
    return total;
}

} // namespace aic
