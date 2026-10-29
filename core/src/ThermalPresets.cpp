#include <nsi/core/ThermalPresets.h>
#include <cmath>
#include <stdexcept>

namespace nsi::core {
namespace {
bool nonnegative(double x) { return std::isfinite(x) && x>=0; }
bool unit(double x) { return nonnegative(x) && x<=1; }
}
void validate(const ThermalGasFilmSpec& s) {
    if (!nonnegative(s.continuumWPerM2K) || !unit(s.accommodation))
        throw std::invalid_argument("Invalid thermal gas-film specification (W/(m2 K), accommodation)");
}
void validate(const ThermalAssemblySpec& s) {
    validate(s.gasFilm);
    if (!nonnegative(s.arealCapacityJPerM2K) || s.arealCapacityJPerM2K==0
        || !unit(s.firstSideCapacityFraction) || s.firstSideCapacityFraction==0 || s.firstSideCapacityFraction==1
        || !nonnegative(s.throughResistanceM2KPerW) || s.throughResistanceM2KPerW==0
        || !nonnegative(s.sheetConductanceWPerK) || !nonnegative(s.junctionConductanceWPerMK))
        throw std::invalid_argument("Invalid thermal assembly capacity/resistance/local conductance (SI)");
    for (const auto side : {s.firstSide,s.secondSide})
        if (!unit(side.emissivity) || !unit(side.absorptivity))
            throw std::invalid_argument("Invalid thermal surface optics");
}
ThermalAssemblySpec nativeThermalSpec(Installation::HullType type) {
    ThermalAssemblySpec s{400,0.5,0,{0.8,0.4},{},0.02,0.1,{20,0.8}};
    switch (type) {
    case Installation::HullType::BareSkin:
        s.throughResistanceM2KPerW=0.08422794320237184;s.secondSide={0.8,0.4};break;
    case Installation::HullType::StandardInsulated:
        s.throughResistanceM2KPerW=1.6328624074472875;s.secondSide={0.85,0.2};break;
    case Installation::HullType::MultiLayerBlanket:
        s.throughResistanceM2KPerW=19.89308783467953;s.secondSide={0.03,0.15};break;
    default: throw std::invalid_argument("Unknown Hull thermal preset");
    }
    validate(s);return s;
}
ThermalAssemblySpec nativeThermalSpec(Installation::PartitionType type) {
    ThermalAssemblySpec s{2000,0.5,0,{0.8,0.4},{0.8,0.4},0.1,0.1,{20,0.8}};
    switch (type) {
    case Installation::PartitionType::BarePartition: s.throughResistanceM2KPerW=0.1186077606265792;break;
    case Installation::PartitionType::ThermalBreak: s.throughResistanceM2KPerW=0.9186101039856908;break;
    case Installation::PartitionType::InsulatedPartition: s.throughResistanceM2KPerW=3.2519439103423147;break;
    default: throw std::invalid_argument("Unknown Partition thermal preset");
    }
    validate(s);return s;
}
} // namespace nsi::core
