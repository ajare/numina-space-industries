#include <nsi/systems/CellAtmosphere.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace {
void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void audit(const CellAtmosphereTransport& runtime) {
    const auto& s=runtime.structure();
    require(std::abs(s.residualJ)<1e-6L+1e-11L*std::abs(s.startingEnergyJ),"Local thermal energy audit failed");
}
void gasInjection() {
    I room(3,1);const auto authored=room.serialize();
    CellAtmosphereTransport runtime(room);
    const auto initial=runtime.fluid().states()[0].q;
    const auto untouched=runtime.fluid().states()[1].q;
    for (std::size_t gas=0;gas<4;++gas) {
        const CellGasInjection source{0,gas,.1,293.15};
        require(runtime.inject(source,.5),"Paused source refused");
        require(std::abs(runtime.fluid().states()[0].q[gas]-initial[gas]-.05)<1e-12,"Paused source mass/rate mismatch");
        require(std::abs(runtime.sectors()[0].ledger.sourceKg[gas]-.05)<1e-12,"Injected species not booked as source");
    }
    require(runtime.elapsedSeconds()==0 && runtime.fluid().states()[1].q==untouched,"Paused injection transported gas or advanced time");
    require(runtime.fluid().states()[0].q[4]==initial[4] && runtime.fluid().states()[0].q[5]==initial[5],"Injection added hidden momentum");
    require(runtime.sectors()[0].ledger.inventoryEnergyJ>0 && runtime.sectors()[0].ledger.externalHeatJ==0,"Injection energy missing/double-booked as heat");audit(runtime);
    const auto before=std::vector<station::CellFluidState>(runtime.fluid().states().begin(),runtime.fluid().states().end());
    const auto ledger=runtime.sectors()[0].ledger;const auto paths=runtime.structure().paths;
    const CellGasInjection source{0,2,.1,293.15};
    require(!runtime.advance(.1,{},4096,0,source),"Injected update ignored thermal budget");
    for (std::size_t i=0;i<before.size();++i) require(runtime.fluid().states()[i].q==before[i].q,"Refusal leaked injected gas/energy");
    require(runtime.sectors()[0].ledger==ledger && runtime.structure().paths==paths && runtime.elapsedSeconds()==0,"Refusal changed injection ledger/time");
    require(runtime.advance(.1,{},4096,4096,source),"Injected update failed retry");audit(runtime);
    require(std::abs(runtime.sectors()[0].ledger.sourceKg[2]-ledger.sourceKg[2]-.01)<1e-12,"Injected source not integrated over simulated seconds");
    require(runtime.fluid().states()[1].q[2]>untouched[2],"Injected gas did not spread to adjacent Cell");
    const auto current=runtime.fluid().states()[0].q;const auto committed=runtime.sectors()[0].ledger;
    for (const CellGasInjection invalid:std::array{CellGasInjection{SIZE_MAX,0,.1,293.15},CellGasInjection{0,4,.1,293.15},
            CellGasInjection{0,0,-1,293.15},CellGasInjection{0,0,.1,-1},CellGasInjection{0,0,1e308,293.15}}) {
        require(!runtime.inject(invalid,1),"Invalid/overflowing source accepted");
        require(runtime.fluid().states()[0].q==current && runtime.sectors()[0].ledger==committed,"Invalid source was not atomic");
    }
    CellAtmosphereTransport witness(room);require(witness.advance(.1,{},4096,4096,source),"Injection work witness failed");
    const auto budget=witness.structure().work.work;require(budget>0,"Thermal fixture did no work");
    CellAtmosphereTransport staged(room);const auto start=staged.fluid().states()[0].q;
    const auto bodyEnergy=staged.structure().solidEnergyJ;
    require(!staged.advance(.2,{},4096,budget,source),"Thermal work budget reset between injected source slices");
    require(staged.fluid().states()[0].q==start && staged.sectors()[0].ledger==CellAtmosphereLedger{}
        && staged.structure().solidEnergyJ==bodyEnergy && staged.elapsedSeconds()==0,"Late refusal committed an injected slice");
    require(room.serialize()==authored,"Injector modified authoring");
}
void thermalDefaults() {
    I room(1,1);room.setHullType(I::HullType::BareSkin);
    const auto authored=room.serialize();
    auto expansion=expandCellAtmosphere(room);
    require(expansion.structure && !expansion.structure->bodies.empty(),"Default expansion omitted structure");
    CellAtmosphereTransport runtime(expansion);
    require(runtime.thermalMode()==CellThermalMode::LocalStructure && runtime.structure().enabled,"Default transport is not LocalStructure");
    const auto initial=runtime.structure().solidEnergyJ;
    require(runtime.advance(.1),"Local thermal update failed");audit(runtime);
    require(runtime.structure().solidEnergyJ<initial && runtime.structure().paths.exteriorRadiationJ>0,"Local surfaces did not radiate");
    require(room.serialize()==authored,"Thermal playback mutated authoring");

    // The native sky is driven by the authored Orbit, not by a hand-authored
    // solar profile: an authored Orbit number reaches a hull temperature.
    expansion.thermal.orbit=I::Orbit{1361.0,5570.0,6791000.0};
    CellAtmosphereTransport solar(expansion);
    require(solar.advance(.1) && solar.structure().paths.absorbedSolarJ>0,"Stellar input did not enter structural ledger");audit(solar);

    const auto bodies=runtime.structure().bodies;
    const auto gas=runtime.fluid().states()[0].q;
    const auto paths=runtime.structure().paths;
    require(!runtime.advance(1,{},4096,0),"Thermal work budget ignored");
    require(runtime.fluid().states()[0].q==gas && runtime.structure().paths==paths && runtime.elapsedSeconds()==.1,"Refusal changed committed gas/ledger/time");
    for (std::size_t i=0;i<bodies.size();++i)
        require(bodies[i].energyJ()==runtime.structure().bodies[i].energyJ(),"Refusal changed solid energy");
    require(runtime.advance(.1),"Retry failed");audit(runtime);

    room.setHullType(std::nullopt);CellAtmosphereTransport off(room);
    require(off.structure().bodies.empty() && off.advance(.1),"No-Hull opt-out gained structural capacity");
    expansion.atmospheres[0].reservoirId="missing";
    bool refused=false;try {CellAtmosphereTransport invalid(expansion);} catch (const std::invalid_argument&) {refused=true;}
    require(refused,"Missing spatial reservoir was accepted");
}
}
int main() {try {gasInjection();thermalDefaults();std::cout<<"Local Cell thermal defaults passed\n";}
catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
