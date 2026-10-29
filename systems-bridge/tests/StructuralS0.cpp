#include <nsi/systems/CellAtmosphere.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace nsi::systems;
namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void step(CellAtmosphereTransport& r) {
    if(!r.advance(.1)) throw std::runtime_error(r.diagnostic());
    require(std::abs(r.structure().residualJ)<1e-6L,"S0 gas/solid/source/escape audit failed");
}
void check(const char* path) {
    std::ifstream input(path);require(input.good(),"Missing portable owner S0 fixture");
    const std::string yaml{std::istreambuf_iterator<char>(input),{}};
    nsi::core::Installation original;original.deserialize(yaml);
    const auto authored=original.serialize();
    const auto& level=original.level(0);
    require(level.width()==16 && level.depth()==16 && level.sectorAt(15,7).name=="S0"
        && level.sectorAt(15,7).area==12,"Owner S0 geometry changed");
    // Open the saved exterior Door; no new opening or authored mutation.
    const auto wall=level.verticalWall(16,7);
    require(level.walls()[wall].type==nsi::core::Installation::WallType::Door,"S0 exterior Door missing");
    auto geometry=expandCellAtmosphere(original,true);
    const auto cell=geometry.cellIndices[0][7*16+15];
    const auto sector=geometry.mesh.cells[cell].atmosphere;
    CellAtmosphereTransport run(geometry,CellThermalMode::LocalStructure);
    require(run.setDoorOpen(0,wall,true),"S0 Door control refused");
    const auto pressure=run.sectors()[sector].meanPressurePa;
    for(int i=0;i<10;++i) step(run);
    require(run.sectors()[sector].meanPressurePa<pressure*.9,"S0 breach did not lose pressure");
    const auto surface=run.structuralSummary(geometry.gasOwners[cell]).all;
    require(run.fluid().primitives()[cell].resolvedGas && surface.surfaces>0
        && std::abs(run.fluid().primitives()[cell].temperatureK-*surface.areaWeightedTemperatureK())>.01,
        "S0 gas and structural temperatures conflated");
    require(run.structure().paths.portalEscapeJ>0,"S0 breach did not radiate");
    // Controlled drain derivative: reach exact evacuation in bounded CI time,
    // without claiming a particular natural blowdown duration/temperature curve.
    auto drained=expandCellAtmosphere(original,true);
    drained.atmospheres[sector].leakRateKgPerS=1e9;
    CellAtmosphereTransport vacuum(drained,CellThermalMode::LocalStructure);
    step(vacuum);
    require(!vacuum.sectors()[sector].resolvedGas,"S0 controlled drain did not evacuate");
    const auto before=vacuum.structuralSummary(drained.gasOwners[cell]).all;
    for(int i=0;i<10;++i) step(vacuum);
    const auto after=vacuum.structuralSummary(drained.gasOwners[cell]).all;
    require(after.capacityJPerK==before.capacityJPerK && after.storedEnergyJ<before.storedEnergyJ,
        "Evacuated S0 lost capacity or stopped evolving");
    require(!vacuum.sectors()[sector].resolvedGas && original.serialize()==authored,"Evacuation created gas or mutated authoring");
    std::cout<<"Owner S0: pressure "<<pressure<<" -> "<<run.sectors()[sector].meanPressurePa
        <<" Pa; gas/surface "<<run.fluid().primitives()[cell].temperatureK<<" / "<<*surface.areaWeightedTemperatureK()<<" K\n";
}
}
int main(int argc,char** argv) {try {require(argc==2,"Expected checked-in S0 fixture path");check(argv[1]);}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
