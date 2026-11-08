#include "blueprint_layout.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aic
{
namespace
{
std::pair<int, int> rotate_direction(int x, int y, int rotation)
{
    switch (rotation)
    {
    case 0:
        return {x, y};
    case 90:
        return {y, -x};
    case 180:
        return {-x, -y};
    case 270:
        return {-y, x};
    default:
        throw std::invalid_argument("Rotation must be 0, 90, 180, or 270 degrees");
    }
}
} // namespace

std::pair<int, int> rotate_cell(int x, int y, int width, int height, int rotation)
{
    switch (rotation)
    {
    case 0:
        return {x, y};
    case 90:
        return {y, width - 1 - x};
    case 180:
        return {width - 1 - x, height - 1 - y};
    case 270:
        return {height - 1 - y, x};
    default:
        throw std::invalid_argument("Rotation must be 0, 90, 180, or 270 degrees");
    }
}

PlacedPort rotate_port(const PlacedPort &port, int width, int height, int rotation)
{
    auto result = port;
    const auto [x, y] = rotate_cell(port.x, port.y, width, height, rotation);
    const auto [fx, fy] = rotate_direction(port.fx, port.fy, rotation);
    result.x = x;
    result.y = y;
    result.fx = fx;
    result.fy = fy;
    return result;
}

PlacedBlueprintEntity rotate_entity(const PlacedBlueprintEntity &entity, int width, int height,
                                    int rotation)
{
    auto result = entity;
    const auto first = rotate_cell(entity.x, entity.y, width, height, rotation);
    const auto last = rotate_cell(entity.x + entity.width - 1, entity.y + entity.height - 1, width,
                                  height, rotation);
    result.x = std::min(first.first, last.first);
    result.y = std::min(first.second, last.second);
    if (rotation == 90 || rotation == 270)
        std::swap(result.width, result.height);
    result.rotation = (entity.rotation + rotation) % 360;
    for (auto &port : result.ports)
        port = rotate_port(port, width, height, rotation);
    return result;
}

int rotate_conveyor_mask(int mask, int rotation)
{
    if (rotation < 0 || rotation > 270 || rotation % 90 != 0)
        throw std::invalid_argument("Rotation must be 0, 90, 180, or 270 degrees");
    constexpr int directions[] = {CONV_NORTH, CONV_EAST, CONV_SOUTH, CONV_WEST};
    int result = 0;
    for (int index = 0; index < 4; ++index)
        if (mask & directions[index])
            result |= directions[(index + rotation / 90) % 4];
    return result;
}

void BlueprintLayout::normalize()
{
    if (entities.empty() && conveyor_cells.empty())
        return;
    int min_x = std::numeric_limits<int>::max(), min_y = min_x;
    int max_x = std::numeric_limits<int>::min(), max_y = max_x;
    auto include = [&](int x, int y)
    {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    };
    for (const auto &entity : entities)
    {
        include(entity.x, entity.y);
        include(entity.x + entity.width - 1, entity.y + entity.height - 1);
    }
    for (const auto &[cell, mask] : conveyor_cells)
        include(cell.first, cell.second);
    for (auto &entity : entities)
    {
        entity.x -= min_x;
        entity.y -= min_y;
        for (auto &port : entity.ports)
        {
            port.x -= min_x;
            port.y -= min_y;
        }
    }
    decltype(conveyor_cells) shifted_cells;
    decltype(conveyor_items) shifted_items;
    for (const auto &[cell, mask] : conveyor_cells)
        shifted_cells[{cell.first - min_x, cell.second - min_y}] = mask;
    for (const auto &[cell, item] : conveyor_items)
        shifted_items[{cell.first - min_x, cell.second - min_y}] = item;
    conveyor_cells = std::move(shifted_cells);
    conveyor_items = std::move(shifted_items);
    bounding_width = max_x - min_x + 1;
    bounding_height = max_y - min_y + 1;
}
} // namespace aic
