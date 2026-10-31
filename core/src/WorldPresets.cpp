#include <nsi/core/WorldPresets.h>

#include <stdexcept>

namespace nsi::core {

std::size_t worldPresetMatching(const Installation::Orbit& orbit) {
    for (std::size_t index = 0; index < kWorldPresets.size(); ++index) {
        const auto& world = kWorldPresets[index];
        if (orbit.stellarIrradianceWPerM2 == world.stellarIrradianceWPerM2
            && orbit.albedo == world.albedo && orbit.axialTiltDeg == world.axialTiltDeg
            && orbit.planetRadiusM == world.planetRadiusM
            && orbit.planetMassKg == world.planetMassKg
            && orbit.siderealRotationS == world.siderealRotationS)
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
    next.planetMassKg = world.planetMassKg;
    next.siderealRotationS = world.siderealRotationS;
    // The height above the surface and the phase offset are left exactly as
    // they were. Which world you are about does not decide how high above it
    // you fly, and the Orbital period is not a field to leave alone any
    // more: it is derived, so it moves because the world moved under it.
    // That is the ride following the world by itself rather than the author
    // having to go and retune a number they never meant to own.
    return next;
}

} // namespace nsi::core
