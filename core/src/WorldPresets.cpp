#include <nsi/core/WorldPresets.h>

#include <stdexcept>

namespace nsi::core {

std::size_t worldPresetMatching(const Installation::Orbit& orbit) {
    for (std::size_t index = 0; index < kWorldPresets.size(); ++index) {
        const auto& world = kWorldPresets[index];
        if (orbit.stellarIrradianceWPerM2 == world.stellarIrradianceWPerM2
            && orbit.albedo == world.albedo && orbit.axialTiltDeg == world.axialTiltDeg
            && orbit.planetRadiusM == world.planetRadiusM)
            return index;
    }
    return kNoWorldPreset;
}

Installation::Orbit applyWorldPreset(const Installation::Orbit& orbit, std::size_t index) {
    if (index >= kWorldPresets.size())
        throw std::out_of_range("No world preset at that index");
    const auto& world = kWorldPresets[index];
    Installation::Orbit next = orbit;
    next.stellarIrradianceWPerM2 = world.stellarIrradianceWPerM2;
    next.albedo = world.albedo;
    next.axialTiltDeg = world.axialTiltDeg;
    next.planetRadiusM = world.planetRadiusM;
    // Keep the altitude the author had above the surface, so a low orbit
    // about Earth stays a low orbit about Jupiter rather than a Hull
    // buried under a gas giant. It only moves when the new world would
    // otherwise have swallowed the orbit: an orbit already high enough is
    // left exactly where it was, because nothing asked for it to move.
    const double altitudeM = orbit.radiusFromPlanetCentreM - orbit.planetRadiusM;
    if (next.radiusFromPlanetCentreM <= next.planetRadiusM)
        next.radiusFromPlanetCentreM = next.planetRadiusM + altitudeM;
    return next;
}

} // namespace nsi::core
