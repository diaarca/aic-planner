#pragma once

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace aic
{

enum class TransportMedium
{
    Solid,
    Pipe
};

struct Port
{
    int x = 0;
    int y = 0;
    TransportMedium type = TransportMedium::Solid;
    int fx = 0; // facing direction dx
    int fy = 0; // facing direction dy
};

struct RecipeIngredient
{
    std::string item;
    double amount = 0;
};

struct RecipeProduct
{
    std::string item;
    double amount = 0;
};

struct Recipe
{
    std::string id;
    bool has_port_bindings = false;
    std::vector<std::string> unsupported_reasons;
    std::string name;
    double time = 0;
    std::vector<RecipeIngredient> ingredients;
    std::vector<RecipeProduct> products;
};

struct Facility
{
    std::string id;
    std::string name;
    double power = 0;
    int width = 0;
    int height = 0;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    std::vector<Recipe> recipes;

    static std::vector<Facility> read_json(const std::string &filename);
    static std::vector<Facility> from_json(const nlohmann::json &data);

    friend std::ostream &operator<<(std::ostream &os, const Facility &f);
    static void print_table(const std::map<std::string, double> &facilities);
};

double facility_power_consumption(const std::map<std::string, double> &counts,
                                  const std::map<std::string, double> &power);

} // namespace aic
