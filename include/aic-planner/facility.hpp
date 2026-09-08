#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Port
{
    int x = 0;
    int y = 0;
    std::string type; // "solid", "liquid", "product"
    int fx = 0;       // facing direction dx
    int fy = 0;       // facing direction dy
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
    std::string name;
    double time = 0;
    std::vector<RecipeIngredient> ingredients;
    std::vector<RecipeProduct> products;
};

struct Facility
{
    std::string name;
    double power = 0;
    int width = 0;
    int height = 0;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    std::vector<Recipe> recipes;

    static std::vector<Facility> readJSON(const std::string& filename);
    static std::map<std::string, double> readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Facility& f);
    static void print_table(const std::map<std::string, double>& facilities);
};
