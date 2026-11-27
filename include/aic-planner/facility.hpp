#pragma once

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aic
{

enum class TransportMedium
{
    Solid,
    Pipe
};

enum class MaterialPhase
{
    Solid,
    Liquid,
    Gas
};

inline constexpr int solid_belt_seconds_per_item = 2;
inline constexpr double pipe_units_per_second = 2;
MaterialPhase material_phase(const std::string &name);
double transport_capacity(MaterialPhase phase);

struct Port
{
    int x = 0;
    int y = 0;
    TransportMedium type = TransportMedium::Solid;
    int fx = 0; // facing direction dx
    int fy = 0; // facing direction dy
    int source_index = -1;
};

struct RecipeIngredient
{
    std::string item;
    double amount = 0;
    MaterialPhase phase = MaterialPhase::Solid;
    std::vector<int> ports;
};

struct RecipeProduct
{
    std::string item;
    double amount = 0;
    MaterialPhase phase = MaterialPhase::Solid;
    std::vector<int> ports;
};

struct ActivationFeed
{
    std::string item;
    MaterialPhase phase = MaterialPhase::Solid;
    double rate = 0; // per second
    int input = -1;  // source catalog port index
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
    int environment = 0;
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
    bool needs_power = false;
    std::optional<std::pair<int, int>> power_range_extension;
    std::optional<ActivationFeed> activation;
    std::map<std::string, double> disposal_rates;

    static std::vector<Facility> read_json(const std::string &filename);
    static std::vector<Facility> from_json(const nlohmann::json &data);

    friend std::ostream &operator<<(std::ostream &os, const Facility &f);
    static void print_table(const std::map<std::string, double> &facilities);
};

double facility_power_consumption(const std::map<std::string, double> &counts,
                                  const std::map<std::string, double> &power);

} // namespace aic
