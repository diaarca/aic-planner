#pragma once

#include "blueprint_layout.hpp"
#include <filesystem>

namespace aic
{
nlohmann::json enkad_blueprint(const BlueprintLayout &layout,
                               const std::vector<Facility> &facilities);
std::filesystem::path write_enkad_blueprint(const std::filesystem::path &directory,
                                            const BlueprintLayout &layout,
                                            const std::vector<Facility> &facilities);
} // namespace aic
