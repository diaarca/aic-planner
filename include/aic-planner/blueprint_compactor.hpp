#pragma once

#include "conveyor_router.hpp"

namespace aic
{
void compact_routed_blueprint(BlueprintLayout &layout, const std::vector<StreamLink> &streams,
                              std::vector<std::pair<int, int>> reserved_approaches,
                              std::chrono::steady_clock::time_point repair_deadline);
} // namespace aic
