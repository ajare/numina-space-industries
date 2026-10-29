#pragma once
#include "station/CellFluid.h"

namespace station {
struct CellObservationOptions {
    bool windDiagnostics{true},objectForces{true};
    bool operator==(const CellObservationOptions&) const = default;
};
// A small passive probe, not a simulated obstacle. Area is projected normal to
// relative flow; callers supply orientation/shape-dependent area and Cd.
// Defaults define the stationary reference object used by the Editor overlay.
struct CellAerodynamicBody {
    std::array<double,2> velocityMS{};
    double areaM2{1},volumeM3{1},dragCoefficient{1};
};
struct CellAerodynamicLoad {
    std::array<double,2> dragN{},pressureN{},forceN{};
    double relativeSpeedMS{},dynamicPressurePa{};
    bool available{}; // false for unresolved gas or unrepresentable results
};
// One-way engineering estimate: drag + undisturbed pressure-gradient force.
// No rigid-body integration, added mass, lift, torque, rarefied/Mach corrections
// or opposite gas booking. Invalid body parameters throw; unresolved gas does not.
CellAerodynamicLoad cellAerodynamicLoad(const CellFluidPrimitive&,
    const std::array<double,2>& pressureGradientPaPerM,const CellAerodynamicBody& = {});

// Preallocated Cell-centred finite-volume pressure gradient. Open faces use the
// mean adjacent pressure (exterior = vacuum); closed faces use owner pressure.
// Apertures/Cd and storage height match the gas mesh. No cross-Wall/Level stencil.
// update must follow a successful gas advance or opening change before sampling.
class CellAerodynamicField {
public:
    explicit CellAerodynamicField(std::size_t cells):gradient_(cells) {}
    void update(const CellFluidSolver&);
    std::span<const std::array<double,2>> pressureGradients() const {return gradient_;}
    CellAerodynamicLoad sample(const CellFluidSolver&,std::size_t cell,const CellAerodynamicBody& = {}) const;
private:
    std::vector<std::array<double,2>> gradient_;
};
} // namespace station
