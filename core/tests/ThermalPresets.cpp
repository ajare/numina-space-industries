#include <nsi/core/Core.h>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace nsi::core;
namespace {
void require(bool x) { if (!x) throw std::runtime_error("Thermal preset mismatch"); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid thermal preset accepted");
}
void check() {
    using I=Installation;
    static_assert(static_cast<int>(I::HullType::BareSkin)==0);
    static_assert(static_cast<int>(I::HullType::StandardInsulated)==1);
    static_assert(static_cast<int>(I::HullType::MultiLayerBlanket)==2);
    static_assert(static_cast<int>(I::PartitionType::BarePartition)==0);
    static_assert(static_cast<int>(I::PartitionType::ThermalBreak)==1);
    static_assert(static_cast<int>(I::PartitionType::InsulatedPartition)==2);
    require(NativeThermalPresetRevision=="C1-v1-provisional");
    const std::array hull{I::HullType::BareSkin,I::HullType::StandardInsulated,I::HullType::MultiLayerBlanket};
    const std::array partition{I::PartitionType::BarePartition,I::PartitionType::ThermalBreak,I::PartitionType::InsulatedPartition};
    const std::array hr{0.08422794320237184,1.6328624074472875,19.89308783467953};
    const std::array pr{0.1186077606265792,0.9186101039856908,3.2519439103423147};
    for (std::size_t i=0;i<3;++i) {
        auto h=nativeThermalSpec(hull[i]),p=nativeThermalSpec(partition[i]);
        validate(h);validate(p);
        require(h.arealCapacityJPerM2K==400 && p.arealCapacityJPerM2K==2000);
        require(h.throughResistanceM2KPerW==hr[i] && p.throughResistanceM2KPerW==pr[i]);
        require(h.sheetConductanceWPerK==0.02 && p.sheetConductanceWPerK==0.1);
        require(h.secondSide.emissivity==I::hullSpec(hull[i]).emissivity);
        require(h.secondSide.absorptivity==I::hullSpec(hull[i]).absorptivity);
        for (const auto& s:{h,p}) {
            require(s.gasFilm.continuumWPerM2K==20 && s.gasFilm.accommodation==0.8);
            require(s.firstSide.emissivity==0.8 && s.junctionConductanceWPerMK==0.1);
            require(s.firstSideCapacityFraction==0.5);
            for (double area:{6.,12.,18.}) {
                const double leaf=1.8*s.arealCapacityJPerM2K,frame=(area-1.8)*s.arealCapacityJPerM2K;
                require(std::abs(leaf+frame-area*s.arealCapacityJPerM2K)<1e-10);
            }
        }
    }
    rejects([] { nativeThermalSpec(static_cast<I::HullType>(-1)); });
    rejects([] { nativeThermalSpec(static_cast<I::PartitionType>(99)); });
    const auto good=nativeThermalSpec(hull[0]);
    using S=ThermalAssemblySpec;
    for (auto field:{&S::arealCapacityJPerM2K,&S::throughResistanceM2KPerW,&S::sheetConductanceWPerK,
            &S::junctionConductanceWPerMK,&S::firstSideCapacityFraction})
        for (double bad:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            auto s=good;s.*field=bad;rejects([&] { validate(s); });
        }
    for (auto field:{&S::arealCapacityJPerM2K,&S::throughResistanceM2KPerW,&S::firstSideCapacityFraction}) {
        auto s=good;s.*field=0;rejects([&] { validate(s); });
    }
    auto s=good;s.firstSideCapacityFraction=1;rejects([&] { validate(s); });
    s=good;s.secondSide.emissivity=1.01;rejects([&] { validate(s); });
    s=good;s.firstSide.absorptivity=-0.1;rejects([&] { validate(s); });
    s=good;s.gasFilm.accommodation=2;rejects([&] { validate(s); });
    s=good;s.gasFilm.continuumWPerM2K=-1;rejects([&] { validate(s); });
    // Zero lateral conductance/optics/film are passive explicit opt-outs.
    s=good;s.sheetConductanceWPerK=0;s.junctionConductanceWPerMK=0;
    s.firstSide={0,0};s.gasFilm={0,0};validate(s);
}
}
int main() { try { check(); } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
