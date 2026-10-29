#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace nsi::systems {
namespace {
using I=core::Installation;
std::string atmosphereId(std::size_t level,std::int64_t uid) {
    return "sector-"+std::to_string(level)+"-"+std::to_string(uid);
}
}
CellAtmosphereExpansion expandCellAtmosphere(const I& installation,bool localStructure) {
    installation.validate();CellAtmosphereExpansion result;
    if (localStructure) result.structure=expandStructuralTopology(installation);
    result.atmospheres=expandInstallation(installation);result.thermal=expandThermal(installation);
    std::map<std::string,std::size_t> groups;
    for (std::size_t i=0;i<result.atmospheres.size();++i) groups.emplace(result.atmospheres[i].id,i);
    result.cellIndices.resize(installation.levelCount());
    for (std::size_t l=0;l<installation.levelCount();++l) {
        const auto& grid=installation.level(l);auto& indices=result.cellIndices[l];indices.assign(grid.cellCount(),SIZE_MAX);
        std::vector<std::size_t> sectorGroups(grid.sectors().size(),SIZE_MAX);
        for (std::size_t s=0;s<grid.sectors().size();++s) {
            const auto found=groups.find(atmosphereId(l,grid.sectors()[s].uid));
            if (found!=groups.end()) sectorGroups[s]=found->second;
        }
        for (std::size_t i=0;i<grid.cellCount();++i) {
            const auto s=grid.cells()[i].sector;const auto g=sectorGroups[s];if (g==SIZE_MAX) continue;
            const auto& source=result.atmospheres[g];const double volume=core::CellVolumeM3*grid.sectors()[s].height;
            const double fraction=volume/source.volumeM3;
            std::array<double,4> mass{source.startingOxygenKg*fraction,source.startingNitrogenKg*fraction,
                source.startingCarbonDioxideKg*fraction,source.startingWaterVapourKg*fraction};
            result.gasOwners.push_back({l,std::int64_t(grid.offsetX())+std::int64_t(i%grid.width()),std::int64_t(grid.offsetY())+std::int64_t(i/grid.width())});
            indices[i]=result.mesh.cells.size();
            result.mesh.cells.push_back({volume,l,i,g,station::makeCellFluidState(mass,source.startingTemperatureC+station::StationCelsiusOffset)});
        }
    }
    std::map<std::pair<std::string,std::string>,std::size_t> thermalLinks;
    for (std::size_t i=0;i<result.thermal.links.size();++i) {
        const auto& link=result.thermal.links[i];
        thermalLinks.emplace(std::minmax(link.aAtmosphereId,link.bAtmosphereId),i);
    }
    auto contact=[&](std::size_t a,std::size_t b,double area) {
        const auto& aid=result.atmospheres[result.mesh.cells[a].atmosphere].id;
        const auto bid=b==SIZE_MAX ? std::string("hull") : result.atmospheres[result.mesh.cells[b].atmosphere].id;
        const auto found=thermalLinks.find(std::minmax(aid,bid));if (found==thermalLinks.end() || !(area>0)) return;
        const auto link=found->second;
        if (result.thermal.links[link].aAtmosphereId!=aid) std::swap(a,b);
        if (a==SIZE_MAX) throw std::logic_error("Expanded Hull link has reversed endpoints");
        result.thermalContacts.push_back({a,b,link,area,0});
    };
    std::vector<double> skin(result.mesh.cells.size(),0);
    for (std::size_t l=0;l<installation.levelCount();++l) {
        const auto& grid=installation.level(l);const auto horizontal=std::size_t(grid.width())*(std::size_t(grid.depth())+1);
        struct Endpoint {std::size_t cell{SIZE_MAX};bool exterior{true};};
        auto endpoint=[&](std::int64_t x,std::int64_t y) {
            if (x<0 || y<0 || x>=grid.width() || y>=grid.depth()) return Endpoint{};
            return Endpoint{result.cellIndices[l][std::size_t(y)*grid.width()+std::size_t(x)],grid.cell(x,y)==I::SectorType::Void};
        };
        for (std::size_t w=0;w<grid.walls().size();++w) {
            const bool alongX=w<horizontal;const auto index=alongX ? w : w-horizontal;
            const auto stride=alongX ? std::size_t(grid.width()) : std::size_t(grid.width())+1;
            const auto x=std::int64_t(index%stride),y=std::int64_t(index/stride);
            const auto left=endpoint(x-(alongX ? 0 : 1),y-(alongX ? 1 : 0)),right=endpoint(x,y);
            const auto type=grid.walls()[w].type;
            const auto door=result.doors.size();
            if (type==I::WallType::Door) result.doors.push_back({l,w,SIZE_MAX,0});
            if (left.cell==SIZE_MAX && right.cell==SIZE_MAX) continue;
            const bool reverse=left.cell==SIZE_MAX;
            const auto a=reverse ? right.cell : left.cell,b=reverse ? SIZE_MAX : right.cell;
            const bool exterior=reverse ? left.exterior : right.exterior;
            const double area=(alongX ? core::CellWidthM : core::CellDepthM)*core::LevelHeightM;
            const bool permitted=b!=SIZE_MAX || exterior;
            station::CellFluidFace face{a,b,alongX ? 1 : 0,reverse ? -1 : 1,area,
                type==I::WallType::None && permitted ? 1.0 : 0.0,1.0};
            if (type==I::WallType::Door && permitted) {
                face.dischargeCoefficient=station::AtmospherePathwaySettings{}.dischargeCoefficient;
                result.doors[door].face=result.mesh.faces.size();
                result.doors[door].fullOpenFraction=core::DoorOpeningAreaM2/area;
            }
            result.mesh.faces.push_back(face);
            const auto& ac=result.mesh.cells[a];
            const auto ah=installation.level(ac.level).sectorAt(ac.gridIndex%grid.width(),ac.gridIndex/grid.width()).height;
            if (b==SIZE_MAX && (type==I::WallType::Exterior || (type==I::WallType::Door && exterior))) skin[a]+=area*ah;
            if (b!=SIZE_MAX && (type==I::WallType::Interior || type==I::WallType::Door)
                && ac.atmosphere!=result.mesh.cells[b].atmosphere) {
                const auto& bc=result.mesh.cells[b];
                const auto bh=grid.sectorAt(bc.gridIndex%grid.width(),bc.gridIndex/grid.width()).height;
                contact(a,b,area*std::min(ah,bh));
            }
        }
    }
    for (std::size_t i=0;i<result.mesh.cells.size();++i) {
        const auto& c=result.mesh.cells[i];const auto& grid=installation.level(c.level);
        const auto x=std::int64_t(c.gridIndex%grid.width()),y=std::int64_t(c.gridIndex/grid.width());
        const auto height=grid.sectorAt(x,y).height;
        const auto wx=std::int64_t(grid.offsetX())+x,wy=std::int64_t(grid.offsetY())+y;
        const auto top=c.level+std::size_t(height);const double floorArea=core::CellWidthM*core::CellDepthM;
        if (!installation.interiorAtWorld(top,wx,wy)) skin[i]+=floorArea;
        if (c.level==0 || !installation.interiorAtWorld(c.level-1,wx,wy)) skin[i]+=floorArea;
        if (top<installation.levelCount()) {
            const auto& upper=installation.level(top);
            const auto ux=wx-upper.offsetX(),uy=wy-upper.offsetY();
            if (ux>=0 && uy>=0 && ux<upper.width() && uy<upper.depth()) {
                const auto b=result.cellIndices[top][std::size_t(uy)*upper.width()+std::size_t(ux)];
                if (b!=SIZE_MAX) contact(i,b,floorArea);
            }
        }
        contact(i,SIZE_MAX,skin[i]);
    }
    std::vector<double> areas(result.thermal.links.size(),0);
    for (const auto& c:result.thermalContacts) areas[c.link]+=c.areaM2;
    for (auto& c:result.thermalContacts) c.conductanceKWPerK=result.thermal.links[c.link].conductanceKWPerK*c.areaM2/areas[c.link];
    for (std::size_t i=0;i<areas.size();++i) if (!(areas[i]>0)) throw std::logic_error("Thermal link has no Cell contacts");
    return result;
}

CellAtmosphereTransport::CellAtmosphereTransport(CellAtmosphereExpansion geometry,CellThermalMode mode)
    :geometry_(std::move(geometry)),fluid_(geometry_.mesh),checkpoint_(fluid_.states().size()),faceTransfers_(fluid_.mesh().faces.size()),
      checkpointTransfers_(faceTransfers_.size()),ledger_(geometry_.atmospheres.size()),checkpointLedger_(ledger_.size()),sectors_(ledger_.size()),mode_(mode) {
    std::vector<double> volumes(ledger_.size(),0);
    for (const auto& c:fluid_.mesh().cells) {
        if (c.atmosphere>=volumes.size()) throw std::invalid_argument("Fluid Cell has no atmosphere group");
        volumes[c.atmosphere]+=c.volumeM3;
    }
    for (std::size_t i=0;i<volumes.size();++i) {
        const auto& a=geometry_.atmospheres[i];
        if (!(a.volumeM3>0) || !std::isfinite(a.volumeM3) || std::abs(volumes[i]-a.volumeM3)>1e-10*a.volumeM3
            || a.occupants<0) throw std::invalid_argument("Invalid fluid atmosphere group");
        for (const double rate:{a.oxygenUsePerOccupantKgPerS,a.co2ProductionPerOccupantKgPerS,
            a.moistureAdditionPerOccupantKgPerS,a.sensibleHeatPerOccupantKW,a.leakRateKgPerS})
            if (!std::isfinite(rate) || rate<0) throw std::invalid_argument("Invalid Cell atmosphere source rate");
    }
    sourceWeights_.reserve(fluid_.states().size());
    for (std::size_t i=0;i<fluid_.states().size();++i) {
        const auto& cell=fluid_.mesh().cells[i];const auto& a=geometry_.atmospheres[cell.atmosphere];
        sourceWeights_.push_back(cell.volumeM3/a.volumeM3);
        if (a.leakRateKgPerS>0 || (a.occupants>0 && (a.oxygenUsePerOccupantKgPerS>0 || a.co2ProductionPerOccupantKgPerS>0
            || a.moistureAdditionPerOccupantKgPerS>0 || a.sensibleHeatPerOccupantKW>0))) sourceCells_.push_back(i);
    }
    if (mode!=CellThermalMode::Adiabatic) {
        for (const auto& a:geometry_.atmospheres) if (!a.reservoirId.empty())
            throw std::invalid_argument("Unresolved spatial thermal reservoir: "+a.reservoirId);
        station::StationSystemsSettings validation;validation.enabled=true;
        validation.hull=geometry_.thermal.hull;validation.solarCycle=geometry_.thermal.solarCycle;
        std::string diagnostic;
        if (!station::validateStationSystemsSettings(validation,diagnostic)) throw std::invalid_argument(diagnostic);
    }
    if (mode==CellThermalMode::LocalStructure) {
        if (!geometry_.structure) throw std::invalid_argument("Local structural topology missing at construction");
        const auto& topology=*geometry_.structure;
        structure_.bodies=topology.bodies;checkpointBodies_=structure_.bodies;
        structuralGas_.resize(fluid_.states().size());
        std::vector<station::ThermalGasContact> gasContacts;
        std::vector<station::ThermalRadiationContact> radiation;
        std::map<StructuralOwner,std::size_t> ownerCells;
        if (geometry_.gasOwners.size()!=fluid_.states().size()) throw std::invalid_argument("Structural gas owner map missing");
        for (std::size_t i=0;i<geometry_.gasOwners.size();++i) ownerCells.emplace(geometry_.gasOwners[i],i);
        for (const auto& assembly:topology.assemblies) for (unsigned side=0;side<2;++side) {
            const auto body=assembly.bodies[side];
            if (const auto& owner=assembly.owners[side]) {
                const auto found=ownerCells.find(*owner);
                if (found!=ownerCells.end()) gasContacts.push_back({body,found->second,assembly.areaM2,
                    {assembly.material.gasFilm.continuumWPerM2K,assembly.material.gasFilm.accommodation}});
            } else {
                const auto material=assembly.surfaceMaterial(side);
                radiation.push_back({body,station::ThermalRadiationBoundary::Exterior,0,assembly.areaM2,material.emissivity,material.absorptivity});
            }
        }
        structuralExterior_.resize(radiation.size());solarWeights_.reserve(radiation.size());
        for (const auto& face:radiation) solarWeights_.push_back(face.absorptivity*face.areaM2/4.0);
        if (!structure_.bodies.empty()) structuralSolver_.emplace(structure_.bodies.size(),structuralGas_.size(),topology.contacts,std::move(gasContacts),std::move(radiation));
        for (const auto& body:structure_.bodies) structure_.startingEnergyJ+=body.energyJ();
        for (const auto& state:fluid_.states()) structure_.startingEnergyJ+=state.q[6];
        structure_.currentEnergyJ=structure_.startingEnergyJ;
    }
    if (mode==CellThermalMode::Geometry) {
        const auto n=fluid_.states().size();
        if (const auto& h=geometry_.thermal.hull) {
            const double capacity=h->heatCapacityKJPerK*1000;
            const long double t=h->startingTemperatureC+station::StationCelsiusOffset;
            const double energy=static_cast<double>(capacity*t);
            if (!std::isfinite(capacity) || !std::isfinite(energy)) throw std::invalid_argument("Spatial Hull energy exceeds finite range");
            thermal_.hull=CellHullThermalState{t,capacity,energy,0,0,0};
        }
        std::vector<station::CellThermalLink> links;links.reserve(geometry_.thermalContacts.size());
        for (const auto& c:geometry_.thermalContacts) {
            if (c.a>=n || (c.b!=SIZE_MAX && c.b>=n) || c.link>=geometry_.thermal.links.size()
                || (c.b==SIZE_MAX && !thermal_.hull)) throw std::invalid_argument("Invalid spatial thermal contact");
            links.push_back({c.a,c.b==SIZE_MAX ? n : c.b,c.conductanceKWPerK*1000});
        }
        if (!links.empty() || thermal_.hull) {
            thermalCapacity_.resize(n+(thermal_.hull ? 1 : 0));thermalTemperature_.resize(thermalCapacity_.size());
            thermalSolver_.emplace(thermalCapacity_.size(),std::move(links));
        }
        thermal_.linkEnergyJ.resize(geometry_.thermal.links.size());checkpointThermalLinks_.resize(thermal_.linkEnergyJ.size());
    }
    publish();momentum_.starting=momentum_.current;
}
void CellAtmosphereTransport::sources(double seconds) {
    for (const auto i:sourceCells_) {
        const auto& cell=fluid_.mesh().cells[i];const auto& a=geometry_.atmospheres[cell.atmosphere];auto& ledger=ledger_[cell.atmosphere];
        const auto beforeMomentum=std::array<double,2>{fluid_.states()[i].q[4],fluid_.states()[i].q[5]};
        const double weight=sourceWeights_[i],people=a.occupants*weight;
        const double demand=people*a.oxygenUsePerOccupantKgPerS*seconds;
        const double t=fluid_.primitives()[i].resolvedGas ? fluid_.primitives()[i].temperatureK : a.startingTemperatureC+station::StationCelsiusOffset;
        const auto oxygen=fluid_.removeSpecies(i,0,demand);
        ledger.oxygenDemandKg+=demand;ledger.oxygenConsumedKg+=oxygen.kg;ledger.sourceKg[0]-=oxygen.kg;ledger.inventoryEnergyJ-=oxygen.energyJ;
        const double fraction=demand>0 ? oxygen.kg/demand : 1;
        const double co2=people*a.co2ProductionPerOccupantKgPerS*seconds*fraction;
        const double vapour=people*a.moistureAdditionPerOccupantKgPerS*seconds;
        if (co2>0) ledger.inventoryEnergyJ+=fluid_.addSpecies(i,2,co2,t);
        if (vapour>0) ledger.inventoryEnergyJ+=fluid_.addSpecies(i,3,vapour,t);
        ledger.sourceKg[2]+=co2;ledger.sourceKg[3]+=vapour;ledger.co2ProducedKg+=co2;ledger.vapourAddedKg+=vapour;
        const double heat=people*a.sensibleHeatPerOccupantKW*seconds*fraction*1000;
        if (heat!=0) fluid_.addHeat(i,heat);
        ledger.occupantHeatJ+=heat;
        const auto mass=fluid_.states()[i];double total=0;for (int g=0;g<4;++g) total+=mass.q[g];
        const double leak=total>0 ? std::min(1.0,a.leakRateKgPerS*weight*seconds/total) : 0;
        if (leak>0) for (int g=0;g<4;++g) {
            const auto removal=fluid_.removeSpecies(i,g,mass.q[g]*leak);
            ledger.sourceKg[g]-=removal.kg;ledger.leakedKg[g]+=removal.kg;ledger.inventoryEnergyJ-=removal.energyJ;
        }
        for (int axis=0;axis<2;++axis) ledger.sourceMomentumKgMS[axis]+=fluid_.states()[i].q[4+axis]-beforeMomentum[axis];
    }
}
bool CellAtmosphereTransport::setDoorOpen(std::size_t level,std::size_t wall,bool open) {
    const auto found=std::find_if(geometry_.doors.begin(),geometry_.doors.end(),
        [&](const auto& d){return d.level==level && d.wall==wall;});
    if (found==geometry_.doors.end()) return false;
    if (found->face!=SIZE_MAX) fluid_.setOpening(found->face,open ? found->fullOpenFraction : 0);
    return true;
}
bool CellAtmosphereTransport::advance(double seconds,std::span<const double> heatJ,std::size_t maxSubsteps,std::size_t maxThermalIterations) {
    diagnostic_.clear();
    if (!std::isfinite(seconds) || seconds<0 || !std::isfinite(elapsed_+seconds)
        || (!heatJ.empty() && heatJ.size()!=fluid_.states().size())) {diagnostic_="Invalid Cell atmosphere update";return false;}
    for (const double heat:heatJ) if (!std::isfinite(heat) || (seconds==0 && heat!=0)) {diagnostic_="Invalid Cell atmosphere heat input";return false;}
    if (seconds==0) return true;
    if (elapsed_+seconds==elapsed_) {diagnostic_="Cell atmosphere duration below clock resolution";return false;}
    const double slices=std::ceil(seconds/0.1);
    if (slices>4096) {diagnostic_="Cell atmosphere source-step work limit exceeded";return false;}
    const auto count=std::size_t(slices);
    std::copy(fluid_.states().begin(),fluid_.states().end(),checkpoint_.begin());
    std::copy(ledger_.begin(),ledger_.end(),checkpointLedger_.begin());
    const auto checkpointMomentum=momentum_;const auto checkpointHull=thermal_.hull;
    const auto previousWork=work_;const auto previousIterations=thermalIterations_;const auto previousResidual=thermalFluxResidual_;
    const auto previousReport=structure_.work;
    const auto previousExterior=structure_.exteriorHeatJ;
    std::copy(structure_.bodies.begin(),structure_.bodies.end(),checkpointBodies_.begin());
    std::copy(faceTransfers_.begin(),faceTransfers_.end(),checkpointTransfers_.begin());
    work_={};thermalIterations_=0;thermalFluxResidual_=0;structure_.work={};
    std::fill(faceTransfers_.begin(),faceTransfers_.end(),station::CellFluidState{});
    std::copy(thermal_.linkEnergyJ.begin(),thermal_.linkEnergyJ.end(),checkpointThermalLinks_.begin());
    try {
        work_.resting=true;double remaining=seconds;
        for (std::size_t slice=0;slice<count;++slice) {
            const double dt=slice+1==count ? remaining : seconds/count;remaining-=dt;
            sources(dt/2);
            const bool advanced=fluid_.advance(dt,maxSubsteps-work_.substeps);
            work_.substeps+=fluid_.work().substeps;work_.faceEvaluations+=fluid_.work().faceEvaluations;
            work_.retries+=fluid_.work().retries;work_.resting=work_.resting && fluid_.work().resting;
            if (!advanced) throw std::runtime_error(fluid_.diagnostic());
            for (int axis=0;axis<2;++axis) momentum_.wallImpulse[axis]+=fluid_.wallImpulse()[axis];
            const auto transfers=fluid_.faceTransfers();
            for (std::size_t f=0;f<transfers.size();++f) {
                for (int q=0;q<7;++q) faceTransfers_[f].q[q]+=transfers[f].q[q];
                const auto& face=fluid_.mesh().faces[f];const auto ga=fluid_.mesh().cells[face.a].atmosphere;
                const auto gb=face.b==SIZE_MAX ? SIZE_MAX : fluid_.mesh().cells[face.b].atmosphere;
                if (ga==gb) continue;
                for (int g=0;g<4;++g) {
                    ledger_[ga].transportedKg[g]-=transfers[f].q[g];
                    if (gb!=SIZE_MAX) ledger_[gb].transportedKg[g]+=transfers[f].q[g];
                    else ledger_[ga].exteriorLostKg[g]+=transfers[f].q[g];
                }
                ledger_[ga].transportedEnergyJ-=transfers[f].q[6];
                if (gb!=SIZE_MAX) ledger_[gb].transportedEnergyJ+=transfers[f].q[6];
                else for (int axis=0;axis<2;++axis) momentum_.exteriorLost[axis]+=transfers[f].q[4+axis];
            }
            sources(dt/2);
            if (mode_==CellThermalMode::LocalStructure)
                structuralStep(dt,elapsed_+seconds-remaining-dt/2,maxThermalIterations-structure_.work.work);
            else thermalStep(dt,elapsed_+seconds-remaining-dt/2,maxThermalIterations-thermalIterations_);
            for (std::size_t i=0;i<fluid_.states().size();++i) {
                auto& ledger=ledger_[fluid_.mesh().cells[i].atmosphere];
                if (!heatJ.empty() && heatJ[i]!=0) {
                    const double heat=heatJ[i]*(dt/seconds);fluid_.addHeat(i,heat);ledger.externalHeatJ+=heat;
                }
                const auto beforeMomentum=std::array<double,2>{fluid_.states()[i].q[4],fluid_.states()[i].q[5]};
                const auto phase=fluid_.condenseVapour(i);
                for (int axis=0;axis<2;++axis) ledger.sourceMomentumKgMS[axis]+=fluid_.states()[i].q[4+axis]-beforeMomentum[axis];
                ledger.condensedKg+=phase.kg;ledger.sourceKg[3]-=phase.kg;ledger.latentHeatJ+=phase.latentJ;
                ledger.condensateEnergyJ+=phase.carriedEnergyJ;ledger.inventoryEnergyJ-=phase.carriedEnergyJ;
            }
        }
        publish();elapsed_+=seconds;return true;
    } catch (const std::exception& e) {
        diagnostic_=e.what();fluid_.restore(checkpoint_);std::copy(checkpointLedger_.begin(),checkpointLedger_.end(),ledger_.begin());
        momentum_=checkpointMomentum;thermal_.hull=checkpointHull;
        std::copy(checkpointThermalLinks_.begin(),checkpointThermalLinks_.end(),thermal_.linkEnergyJ.begin());
        std::copy(checkpointBodies_.begin(),checkpointBodies_.end(),structure_.bodies.begin());
        structure_.exteriorHeatJ=previousExterior;structure_.work=previousReport;
        work_=previousWork;thermalIterations_=previousIterations;thermalFluxResidual_=previousResidual;
        std::copy(checkpointTransfers_.begin(),checkpointTransfers_.end(),faceTransfers_.begin());
        return false;
    }
}
void CellAtmosphereTransport::thermalStep(double seconds,double midpoint,std::size_t maxIterations) {
    if (!thermalSolver_) return;
    const auto n=fluid_.states().size();
    double absorbedKW=0;
    if (const auto& h=geometry_.thermal.hull) {
        const station::ProfileSettings defaultProfile{};
        const auto& profile=geometry_.thermal.solarCycle ? *geometry_.thermal.solarCycle : defaultProfile;
        const double exposure=station::evaluateStationProfile(profile,midpoint);
        absorbedKW=h->absorptivity*h->projectedAreaM2*h->fullSunIrradianceKWPerM2*exposure;
    }
    auto radiate=[&] {
        if (!thermal_.hull) return;
        auto& body=*thermal_.hull;
        const auto result=station::integrateCellHullRadiation(*geometry_.thermal.hull,body.temperatureK,absorbedKW,seconds/2);
        if (!result) throw std::runtime_error("Spatial Hull radiation integration failed");
        body.temperatureK+=result->temperatureChangeK;body.absorbedJ+=result->absorbedJ;body.emittedJ+=result->emittedJ;
    };
    radiate();
    for (std::size_t i=0;i<n;++i) {
        const auto& p=fluid_.primitives()[i];
        thermalCapacity_[i]=p.resolvedGas ? p.capacityJPerK : 0;
        thermalTemperature_[i]=p.resolvedGas ? p.temperatureK : 0;
    }
    if (thermal_.hull) {thermalCapacity_[n]=thermal_.hull->capacityJPerK;thermalTemperature_[n]=thermal_.hull->temperatureK;}
    const bool solved=thermalSolver_->solve(thermalCapacity_,thermalTemperature_,seconds,std::min(std::size_t(256),maxIterations));
    thermalIterations_+=thermalSolver_->iterations();
    thermalFluxResidual_=std::max(thermalFluxResidual_,thermalSolver_->maxFluxResidualJ());
    if (!solved) throw std::runtime_error(thermalSolver_->diagnostic());
    const auto heat=thermalSolver_->heatJ();
    for (std::size_t i=0;i<n;++i) if (heat[i]!=0) {
        fluid_.addHeat(i,heat[i]);ledger_[fluid_.mesh().cells[i].atmosphere].conductedHeatJ+=heat[i];
    }
    if (thermal_.hull) {
        thermal_.hull->temperatureK+=heat[n]/thermal_.hull->capacityJPerK;thermal_.hull->conductedJ+=heat[n];
    }
    const auto transfers=thermalSolver_->transferredJ();
    for (std::size_t i=0;i<transfers.size();++i) thermal_.linkEnergyJ[geometry_.thermalContacts[i].link]+=transfers[i];
    radiate();
}
void CellAtmosphereTransport::structuralStep(double seconds,double midpoint,std::size_t maxWork) {
    if (!structuralSolver_) return;
    constexpr std::array<double,4> molarMass{station::StationMolarMassOxygenKgPerMol,station::StationMolarMassNitrogenKgPerMol,
        station::StationMolarMassCarbonDioxideKgPerMol,station::StationMolarMassWaterKgPerMol};
    for (std::size_t i=0;i<structuralGas_.size();++i) {
        const auto& p=fluid_.primitives()[i];auto& gas=structuralGas_[i];gas={};
        gas.resolved=p.resolvedGas;gas.capacityJPerK=p.capacityJPerK;
        const auto& q=fluid_.states()[i].q;
        gas.energyJ=q[6]-(p.massKg>0 ? (q[4]*q[4]+q[5]*q[5])/(2*p.massKg) : 0);
        if (!p.resolvedGas) continue;
        gas.pressurePaPerK=p.pressurePa/p.temperatureK;
        double moles=0;
        for (int g=0;g<4;++g) {gas.wetMoleFractions[g]=q[g]/molarMass[g];moles+=gas.wetMoleFractions[g];}
        for (auto& fraction:gas.wetMoleFractions) fraction/=moles;
    }
    const station::ProfileSettings dark{0.0};
    const auto& profile=geometry_.thermal.solarCycle ? *geometry_.thermal.solarCycle : dark;
    const double exposure=station::evaluateStationProfile(profile,midpoint);
    const auto& hull=geometry_.thermal.hull;
    const double irradiance=hull ? hull->fullSunIrradianceKWPerM2*1000 : station::HullSettings{}.fullSunIrradianceKWPerM2*1000;
    for (std::size_t i=0;i<structuralExterior_.size();++i) {
        auto& exterior=structuralExterior_[i];
        exterior.spaceTemperatureK=hull ? hull->sinkTemperatureC+station::StationCelsiusOffset : 3;
        exterior.absorbedSolarW=solarWeights_[i]*irradiance*exposure;
    }
    station::NativeThermalBudget budget;budget.maxWork=maxWork;
    if (!structuralSolver_->solve(structure_.bodies,structuralGas_,structuralExterior_,seconds,budget))
        throw std::runtime_error(std::string(structuralSolver_->diagnostic()));
    const auto& report=structuralSolver_->report();auto& work=structure_.work;
    work.work+=report.work;work.intervals+=report.intervals;work.refinements+=report.refinements;
    work.nonlinearIterations+=report.nonlinearIterations;work.linearIterations+=report.linearIterations;
    work.nonlinearResidualJ=std::max(work.nonlinearResidualJ,report.nonlinearResidualJ);
    work.lastTrialNonlinearResidualJ=report.lastTrialNonlinearResidualJ;
    work.fluxResidualJ=std::max(work.fluxResidualJ,report.fluxResidualJ);
    work.energyResidualJ+=report.energyResidualJ;work.exteriorHeatJ+=report.exteriorHeatJ;
    structure_.exteriorHeatJ+=report.exteriorHeatJ;
    thermalIterations_=work.work;thermalFluxResidual_=work.fluxResidualJ;
    const auto heat=structuralSolver_->heatJ();const auto n=structure_.bodies.size();
    for (std::size_t i=0;i<n;++i) structure_.bodies[i].addHeat(heat[i]);
    for (std::size_t i=0;i<structuralGas_.size();++i) if (heat[n+i]!=0) {
        fluid_.addHeat(i,heat[n+i]);ledger_[fluid_.mesh().cells[i].atmosphere].conductedHeatJ+=heat[n+i];
    }
}
void CellAtmosphereTransport::publish() {
    momentum_.current={};momentum_.sources={};
    for (auto& s:sectors_) {s={};s.minPressurePa=std::numeric_limits<double>::max();}
    for (std::size_t i=0;i<fluid_.states().size();++i) {
        const auto& c=fluid_.mesh().cells[i];const auto& p=fluid_.primitives()[i];const auto& q=fluid_.states()[i].q;auto& s=sectors_[c.atmosphere];
        for (int g=0;g<4;++g) s.massKg[g]+=q[g];
        s.volumeM3+=c.volumeM3;s.meanPressurePa+=p.pressurePa*c.volumeM3;
        s.minPressurePa=std::min(s.minPressurePa,p.pressurePa);s.maxPressurePa=std::max(s.maxPressurePa,p.pressurePa);
        s.capacityJPerK+=p.capacityJPerK;s.totalEnergyJ+=q[6];
        for (int axis=0;axis<2;++axis) momentum_.current[axis]+=q[4+axis];
        if (p.massKg>0) s.kineticEnergyJ+=(q[4]*q[4]+q[5]*q[5])/(2*p.massKg);
        s.resolvedGas=s.resolvedGas || p.resolvedGas;
    }
    if (mode_==CellThermalMode::LocalStructure) {
        long double current=0,external=structure_.exteriorHeatJ;
        for (const auto& body:structure_.bodies) current+=body.energyJ();
        structure_.solidEnergyJ=current;structure_.gasInternalEnergyJ=0;structure_.gasKineticEnergyJ=0;
        for (const auto& sector:sectors_) {
            structure_.gasInternalEnergyJ+=static_cast<long double>(sector.totalEnergyJ)-sector.kineticEnergyJ;
            structure_.gasKineticEnergyJ+=sector.kineticEnergyJ;
        }
        for (const auto& state:fluid_.states()) current+=state.q[6];
        for (const auto& ledger:ledger_) external+=static_cast<long double>(ledger.inventoryEnergyJ)+ledger.transportedEnergyJ
            +ledger.externalHeatJ+ledger.occupantHeatJ+ledger.latentHeatJ;
        structure_.currentEnergyJ=current;
        structure_.residualJ=structure_.startingEnergyJ+external-current;
    }
    for (std::size_t i=0;i<sectors_.size();++i) {
        auto& s=sectors_[i];s.ledger=ledger_[i];
        for (int axis=0;axis<2;++axis) momentum_.sources[axis]+=ledger_[i].sourceMomentumKgMS[axis];
        if (s.volumeM3>0) s.meanPressurePa/=s.volumeM3;else s.minPressurePa=0;
        if (s.capacityJPerK>0) s.temperatureK=(s.totalEnergyJ-s.kineticEnergyJ)/s.capacityJPerK;
    }
}
} // namespace nsi::systems
