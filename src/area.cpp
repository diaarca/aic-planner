#include "area.hpp"
#include "blueprint_layout.hpp"
#include "facility.hpp"
#include "json_io.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

namespace aic
{

std::vector<Area> Area::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    std::vector<Area> areas;
    for (const auto &item : j)
    {
        Area a;
        a.name = item.value("area", "");
        a.pac_width = item.value("pac_width", 0.0);
        a.pac_height = item.value("pac_height", 0.0);

        double total_depot_w = 0;
        double total_depot_h = 0;
        if (item.contains("static_depots"))
        {
            for (const auto &d : item["static_depots"])
            {
                StaticDepot sd;
                sd.x1 = d.at("x1");
                sd.y1 = d.at("y1");
                sd.x2 = d.at("x2");
                sd.y2 = d.at("y2");
                if (sd.y1 == sd.y2)
                    total_depot_w += std::abs(sd.x2 - sd.x1) + 1;
                else if (sd.x1 == sd.x2)
                    total_depot_h += std::abs(sd.y2 - sd.y1) + 1;
                else
                    throw std::invalid_argument("Depot segments must be horizontal or vertical");
                a.static_depots.push_back(sd);
            }
        }
        a.pac_depot_width = total_depot_w;
        a.pac_depot_height = total_depot_h;

        a.area_facilities = item.value("facilities", std::map<std::string, double>{});
        if (item.contains("sources"))
            for (const auto &source : item.at("sources"))
                a.sources.push_back({source.at("item"), source.at("facility"), source.at("x"),
                                     source.at("y"), source.at("rotation"), source.at("limit"),
                                     source.value("conduit_connection", "")});
        if (item.contains("depot_bus"))
            for (const auto &piece : item.at("depot_bus"))
                a.depot_bus.push_back(
                    {piece.at("facility"), piece.at("x"), piece.at("y"), piece.at("rotation")});
        if (item.contains("climates"))
            for (const auto &climate : item.at("climates"))
            {
                AreaClimate entry{climate.at("environment"), climate.at("gas"), climate.at("rate"),
                                  climate.at("range_x"), climate.at("range_y")};
                if (!a.climates.emplace(entry.environment, entry).second)
                    throw std::invalid_argument("Duplicate climate in " + a.name);
            }
        if (item.contains("rebuilding_port"))
        {
            const auto &port = item.at("rebuilding_port");
            a.rebuilding_port = DepotBusPiece{"wonder_supply_port_1", port.at("x"), port.at("y"),
                                              port.at("rotation")};
        }
        if (item.contains("array_fields"))
            for (const auto &field : item.at("array_fields").items())
            {
                ArrayField entry;
                entry.belt_rotation = field.value().at("belt_rotation");
                entry.cycle_seconds = field.value().at("cycle_seconds");
                for (const auto &projector : field.value().at("projectors"))
                    entry.projectors.push_back({projector.at("mode"), projector.at("dx"),
                                                projector.at("dy"), projector.at("rotation"),
                                                projector.at("feed")});
                a.array_fields.emplace(field.key(), std::move(entry));
            }
        a.power_pylon = item.value("power_pylon", "");
        a.output_to_depot_bus = item.value("output_to_depot_bus", false);
        a.allow_pipe_over_belt = item.value("allow_pipe_over_belt", false);
        areas.push_back(a);
    }
    return areas;
}

void validate_area_installations(Area &area, const std::vector<Facility> &facilities)
{
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::invalid_argument(message);
    };
    const auto check_rate = [&](double rate, const std::string &key)
    {
        require(std::isfinite(rate) && rate > 0 && rate <= 120,
                area.name + " " + key + " must be greater than zero and at most 120 units/min");
    };
    const auto dimensions = [&](const std::string &id, int x, int y, int rotation)
    {
        const auto found = std::find_if(facilities.begin(), facilities.end(),
                                        [&](const Facility &value) { return value.name == id; });
        require(found != facilities.end(), "Unknown facility " + id + " in " + area.name);
        require(rotation >= 0 && rotation < 360 && rotation % 90 == 0,
                "Invalid rotation for " + id + " in " + area.name);
        const bool swapped = rotation % 180 != 0;
        const int width = swapped ? found->height : found->width;
        const int height = swapped ? found->width : found->height;
        require(x >= 0 && y >= 0 && int64_t{x} + width <= area.pac_width &&
                    int64_t{y} + height <= area.pac_height,
                id + " is outside " + area.name);
        return std::make_pair(width, height);
    };
    std::set<std::pair<int, int>> fixed_tiles;
    const auto reserve = [&](int x, int y, int width, int height)
    {
        for (int i = x; i < x + width; ++i)
            for (int j = y; j < y + height; ++j)
                require(fixed_tiles.emplace(i, j).second,
                        "Overlapping source or Depot Bus footprints in " + area.name);
    };
    for (const auto &source : area.sources)
    {
        check_rate(source.limit, source.item + " source rate");
        const auto [width, height] =
            dimensions(source.facility, source.x, source.y, source.rotation);
        reserve(source.x, source.y, width, height);
    }
    for (const auto &[environment, climate] : area.climates)
    {
        require(environment > 0 && !climate.gas.empty() && climate.range_x >= 0 &&
                    climate.range_x <= 1000 && climate.range_y >= 0 && climate.range_y <= 1000,
                "Invalid climate specification in " + area.name);
        check_rate(climate.limit, "climate disperser rate");
        require(climate.limit == std::floor(climate.limit),
                "Climate feed must use whole units/min in " + area.name);
    }
    const auto rotation_valid = [](int rotation)
    { return rotation >= 0 && rotation < 360 && rotation % 90 == 0; };
    for (const auto &[mode, field] : area.array_fields)
    {
        require((mode == "L" || mode == "T" || mode == "E") &&
                    rotation_valid(field.belt_rotation) && std::isfinite(field.cycle_seconds) &&
                    field.cycle_seconds > 0 && field.cycle_seconds <= 3600,
                "Invalid array mode, belt rotation or cycle time in " + area.name);
        std::set<std::string> projectors;
        for (const auto &projector : field.projectors)
        {
            require((projector.mode == "L" || projector.mode == "T") &&
                        rotation_valid(projector.rotation) &&
                        std::abs(int64_t{projector.dx}) <= area.pac_width &&
                        std::abs(int64_t{projector.dy}) <= area.pac_height,
                    "Invalid array projector geometry in " + area.name);
            require(projectors.insert(projector.mode).second,
                    "Duplicate array projector mode in " + area.name);
            check_rate(projector.feed, "array projector feed");
            require(projector.feed == std::floor(projector.feed),
                    "Array feed must use whole units/min in " + area.name);
        }
        require(projectors ==
                    (mode == "E" ? std::set<std::string>{"L", "T"} : std::set<std::string>{mode}),
                "Array needs matching projector modes in " + area.name);
    }
    std::set<size_t> active;
    for (size_t index = 0; index < area.depot_bus.size(); ++index)
    {
        auto &piece = area.depot_bus[index];
        require(piece.facility == "log_hongs_bus_source" || piece.facility == "log_hongs_bus",
                "Unknown Depot Bus piece in " + area.name + ": " + piece.facility);
        std::tie(piece.width, piece.height) =
            dimensions(piece.facility, piece.x, piece.y, piece.rotation);
        reserve(piece.x, piece.y, piece.width, piece.height);
        if (piece.facility == "log_hongs_bus_source")
            active.insert(index);
    }
    const auto adjacent = [](const DepotBusPiece &a, const DepotBusPiece &b)
    {
        const bool x_overlap = a.x < b.x + b.width && b.x < a.x + a.width;
        const bool y_overlap = a.y < b.y + b.height && b.y < a.y + a.height;
        return (x_overlap && (a.y + a.height == b.y || b.y + b.height == a.y)) ||
               (y_overlap && (a.x + a.width == b.x || b.x + b.width == a.x));
    };
    for (size_t previous = 0; previous != active.size();)
    {
        previous = active.size();
        for (size_t index = 0; index < area.depot_bus.size(); ++index)
            if (!active.count(index) &&
                std::any_of(active.begin(), active.end(),
                            [&](size_t linked)
                            { return adjacent(area.depot_bus[index], area.depot_bus[linked]); }))
                active.insert(index);
    }
    require(active.size() == area.depot_bus.size(),
            "Depot Bus Section disconnected from every Depot Bus Port in " + area.name);
    require(!area.output_to_depot_bus || !area.depot_bus.empty(),
            "output_to_depot_bus requires a connected Depot Bus in " + area.name);
    if (area.rebuilding_port)
    {
        auto &port = *area.rebuilding_port;
        std::tie(port.width, port.height) =
            dimensions(port.facility, port.x, port.y, port.rotation);
        reserve(port.x, port.y, port.width, port.height);
    }
    if (!area.power_pylon.empty())
        require(std::any_of(facilities.begin(), facilities.end(),
                            [&](const Facility &facility)
                            {
                                return (facility.id == area.power_pylon ||
                                        facility.name == area.power_pylon) &&
                                       facility.power_range_extension.has_value();
                            }),
                "Unknown power pylon in " + area.name + ": " + area.power_pylon);
}

std::vector<std::tuple<int, int, int>> depot_attachments(const Area &area, int width)
{
    std::set<std::pair<int, int>> occupied;
    for (const auto &piece : area.depot_bus)
        for (int x = piece.x; x < piece.x + piece.width; ++x)
            for (int y = piece.y; y < piece.y + piece.height; ++y)
                occupied.emplace(x, y);

    std::vector<std::tuple<int, int, int>> attachments;
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        const auto [lx, ly] = rotate_cell(0, 1, width, 1, rotation * 90);
        const auto [rx, ry] = rotate_cell(width - 1, 1, width, 1, rotation * 90);
        const auto [fx, fy] = rotate_cell(0, 0, width, 1, rotation * 90);
        const int dx = rx > lx ? 1 : rx < lx ? -1 : 0;
        const int dy = ry > ly ? 1 : ry < ly ? -1 : 0;
        for (const auto &[x, y] : occupied)
        {
            const int ax = x - lx, ay = y - ly;
            if (ax < 0 || ay < 0 || ax + (rotation % 2 ? 1 : width) > area.pac_width ||
                ay + (rotation % 2 ? width : 1) > area.pac_height)
                continue;
            bool attached = true;
            for (int i = 0; i < width; ++i)
                attached &= occupied.count({x + i * dx, y + i * dy}) &&
                            !occupied.count({ax + fx + i * dx, ay + fy + i * dy});
            if (attached)
                attachments.emplace_back(ax, ay, rotation);
        }
    }
    return attachments;
}

std::ostream &operator<<(std::ostream &os, const Area &a)
{
    os << "Area: " << std::left << std::setw(20) << a.name << " | Size: " << a.pac_width << "x"
       << a.pac_height;
    return os;
}

void Area::print_table(const std::vector<Area> &areas)
{
    std::cout << "\n--- Areas Table ---\n";
    std::cout << std::left << std::setw(20) << "Area"
              << " | " << std::setw(10) << "Width"
              << " | " << std::setw(10) << "Height"
              << " | " << std::setw(8) << "Zip"
              << " | " << std::setw(8) << "Def"
              << " | " << std::setw(8) << "Mine"
              << "\n";
    std::cout << std::string(85, '-') << "\n";

    for (const auto &a : areas)
    {
        std::cout << std::left << std::setw(20) << a.name << " | " << std::setw(10) << a.pac_width
                  << " | " << std::setw(10) << a.pac_height << " | " << std::setw(8)
                  << a.area_facilities.at("zipline") << " | " << a.area_facilities.at("defense")
                  << " | " << a.area_facilities.at("mining_rig") << "\n";
    }
}

} // namespace aic
