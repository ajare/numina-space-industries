#include "station/CellAerodynamics.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace station {
CellAerodynamicLoad cellAerodynamicLoad(const CellFluidPrimitive& gas,
    const std::array<double,2>& gradient,const CellAerodynamicBody& body) {
    if (!std::isfinite(body.areaM2) || body.areaM2<0 || !std::isfinite(body.volumeM3) || body.volumeM3<0
        || !std::isfinite(body.dragCoefficient) || body.dragCoefficient<0
        || !std::isfinite(body.velocityMS[0]) || !std::isfinite(body.velocityMS[1]))
        throw std::invalid_argument("Invalid aerodynamic body");
    CellAerodynamicLoad load;
    if (!gas.resolvedGas || !std::isfinite(gas.density) || gas.density<0
        || !std::isfinite(gradient[0]) || !std::isfinite(gradient[1])) return load;
    const std::array<double,2> relative{gas.velocityX-body.velocityMS[0],gas.velocityY-body.velocityMS[1]};
    load.relativeSpeedMS=std::hypot(relative[0],relative[1]);
    if (!std::isfinite(load.relativeSpeedMS)) return {};
    load.dynamicPressurePa=0.5*gas.density*load.relativeSpeedMS*load.relativeSpeedMS;
    const double drag=load.dynamicPressurePa*body.dragCoefficient*body.areaM2;
    if (!std::isfinite(drag) || !std::isfinite(load.dynamicPressurePa)) return {};
    for (int axis=0;axis<2;++axis) {
        load.dragN[axis]=load.relativeSpeedMS>0 ? drag*(relative[axis]/load.relativeSpeedMS) : 0;
        load.pressureN[axis]=-body.volumeM3*gradient[axis];
        load.forceN[axis]=load.dragN[axis]+load.pressureN[axis];
        if (!std::isfinite(load.pressureN[axis]) || !std::isfinite(load.forceN[axis])) return {};
    }
    if (!std::isfinite(std::hypot(load.forceN[0],load.forceN[1]))) return {};
    load.available=true;return load;
}
void CellAerodynamicField::update(const CellFluidSolver& fluid) {
    if (fluid.states().size()!=gradient_.size()) throw std::invalid_argument("Aerodynamic field size mismatch");
    std::fill(gradient_.begin(),gradient_.end(),std::array<double,2>{});
    const auto& mesh=fluid.mesh();const auto p=fluid.primitives();
    for (const auto& face:mesh.faces) {
        if (face.openFraction==0) continue;
        const double other=face.b==SIZE_MAX ? 0 : p[face.b].pressurePa;
        const double jump=face.sign*(other-p[face.a].pressurePa);
        const double area=0.5*face.areaM2*face.openFraction*face.dischargeCoefficient;
        gradient_[face.a][face.axis]+=jump*(area/mesh.cells[face.a].volumeM3);
        if (face.b!=SIZE_MAX) gradient_[face.b][face.axis]+=jump*(area/mesh.cells[face.b].volumeM3);
    }
}
CellAerodynamicLoad CellAerodynamicField::sample(const CellFluidSolver& fluid,std::size_t cell,const CellAerodynamicBody& body) const {
    if (cell>=gradient_.size() || fluid.states().size()!=gradient_.size()) throw std::out_of_range("Invalid aerodynamic Cell");
    return cellAerodynamicLoad(fluid.primitives()[cell],gradient_[cell],body);
}
} // namespace station
