#pragma once
#include "station/StationSystems.h"
namespace station_test {
// Runtime scenarios need only the authored settings, not client document metadata.
struct ReferenceFixture { station::StationSystemsSettings settings; };
ReferenceFixture const& bundledStationReference();
}
