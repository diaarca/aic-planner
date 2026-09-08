#include "facility.hpp"
#include <csv.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

static void determinePortFacing(Port& p, int w, int h)
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

std::vector<Facility> Facility::readJSON(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open facilities JSON file: " +
                                 filename);
    }
    nlohmann::json j;
    file >> j;

    std::vector<Facility> facilities;
    for (const auto& item : j)
    {
        Facility f;
        f.name = item.value("name", "");
        f.power = item.value("power", 0.0);
        f.width = item.value("width", 0);
        f.height = item.value("height", 0);

        if (item.contains("inputs"))
        {
            for (const auto& in : item["inputs"])
            {
                Port p;
                p.x = in.value("x", 0);
                p.y = in.value("y", 0);
                p.type = in.value("type", "product");
                determinePortFacing(p, f.width, f.height);
                f.inputs.push_back(p);
            }
        }

        if (item.contains("outputs"))
        {
            for (const auto& out : item["outputs"])
            {
                Port p;
                p.x = out.value("x", 0);
                p.y = out.value("y", 0);
                p.type = out.value("type", "product");
                determinePortFacing(p, f.width, f.height);
                f.outputs.push_back(p);
            }
        }

        if (item.contains("recipes"))
        {
            for (const auto& rec : item["recipes"])
            {
                Recipe r;
                r.name = rec.value("name", "");
                r.time = rec.value("time", 0.0);

                if (rec.contains("ingredients"))
                {
                    for (const auto& ing : rec["ingredients"])
                    {
                        RecipeIngredient ri;
                        ri.item = ing.value("item", "");
                        ri.amount = ing.value("amount", 0.0);
                        r.ingredients.push_back(ri);
                    }
                }

                if (rec.contains("products"))
                {
                    for (const auto& prod : rec["products"])
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

std::map<std::string, double> Facility::readCSV(const std::string& filename)
{
    std::map<std::string, double> results;
    io::CSVReader<2> in(filename);
    in.read_header(io::ignore_extra_column, "facility", "power");
    std::string name;
    double power;
    while (in.read_row(name, power))
    {
        results[name] = power;
    }
    return results;
}

std::ostream& operator<<(std::ostream& os, const Facility& f)
{
    os << "Facility: " << std::left << std::setw(20) << f.name
       << " | Power: " << f.power;
    return os;
}

void Facility::print_table(const std::map<std::string, double>& facilities)
{
    std::cout << "\n--- Facilities Table ---\n";
    std::cout << std::left << std::setw(25) << "Facility"
              << " | " << std::setw(10) << "Power"
              << "\n";
    std::cout << std::string(40, '-') << "\n";

    for (const auto& [name, power] : facilities)
    {
        std::cout << std::left << std::setw(25) << name << " | "
                  << std::setw(10) << power << "\n";
    }
}
