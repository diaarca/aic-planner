#include "facility.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace aic
{

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

                if (rec.contains("ingredients"))
                {
                    for (const auto &ing : rec["ingredients"])
                    {
                        RecipeIngredient ri;
                        ri.item = ing.value("item", "");
                        ri.amount = ing.value("amount", 0.0);
                        r.ingredients.push_back(ri);
                    }
                }

                if (rec.contains("products"))
                {
                    for (const auto &prod : rec["products"])
                    {
                        RecipeProduct rp;
                        rp.item = prod.value("item", "");
                        rp.amount = prod.value("amount", 0.0);
                        r.products.push_back(rp);
                    }
                }
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
