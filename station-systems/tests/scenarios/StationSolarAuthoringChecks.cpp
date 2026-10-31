#include "Smoke.h"
#include "station/StationSystemsRuntime.h"

#include <cmath>
#include <vector>

namespace
{
// #60 is a prefactor: even the newly authored fields must not change the
// capacity-and-solar-cycle calculation until the generation ticket lands.
void solarArrayAuthoringOnly(smoke::Context const&)
{
    for (auto const technology : {station::SolarCellTechnology::TripleJunctionGaAs,
            station::SolarCellTechnology::SingleJunctionGaAs, station::SolarCellTechnology::SpaceSilicon})
    for (double area : {0.0, 61.5})
    for (bool enabled : {false, true})
    for (double sunlight : {0.0, 0.25, 1.0})
    {
        station::StationSystemsSettings settings;
        settings.enabled = true;
        station::SolarArraySettings array;
        array.id = "sun";
        array.capacityKW = 20.0;
        array.areaM2 = area;
        array.cellTechnology = technology;
        array.enabled = enabled;
        settings.solarArrays = {array};
        settings.solarCycle.emplace().periodSeconds = 10;
        settings.solarCycle->segments = {{0, sunlight}};
        station::StationSystemsRuntime runtime(settings, 1.0f / 60.0f);
        smoke::require(runtime.advanceTo(1.0) && runtime.snapshot().healthy,
            "authoring-field prefactor broke the runtime");
        smoke::require(std::abs(runtime.snapshot().power.poolGenerationKW - 20.0 * sunlight) < 1e-10,
            "authoring-only solar fields changed capacity-based generation");
    }
}
}

void registerStationSolarAuthoringChecks(std::vector<smoke::Check>& checks)
{
    checks.push_back({"solarArrayAuthoringOnly", solarArrayAuthoringOnly});
}
