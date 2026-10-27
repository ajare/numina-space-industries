#pragma once
#include "Smoke.h"
#include "StationRuntimeFixtures.h"
#include "station/StationSystemsRuntime.h"
#include "station/StationPorts.h"
#include "station/StationPowerBuses.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace smoke::station {
inline void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
}
