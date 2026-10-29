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

    expansion.thermal.solarCycle=station::ProfileSettings{1.0};
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
int main() {try {thermalDefaults();std::cout<<"Local Cell thermal defaults passed\n";}
catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
