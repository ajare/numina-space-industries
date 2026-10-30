#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace nsi::systems {
void CellAtmosphereTransport::buildStructuralObservations() {
    const auto& t=*geometry_.structure;
    ownerCavities_={t.cavityOwners.begin(),t.cavityOwners.end()};
    for(std::size_t i=0;i<geometry_.gasOwners.size();++i) ownerGas_.emplace(geometry_.gasOwners[i],i);
    surfaceOwners_.resize(t.bodies.size());surfaceSlots_.resize(t.bodies.size());
    bodyContacts_.resize(t.bodies.size());cavitySurfaces_.resize(t.cavityCount);
    cavityPortals_.resize(t.cavityCount);committedIrradiance_.resize(t.cavityCount);
    for(std::size_t i=0;i<observedPortals_.size();++i) {
        const auto& p=observedPortals_[i];cavityPortals_[p.a].push_back(i);
        if(p.b && *p.b!=p.a) cavityPortals_[*p.b].push_back(i);
    }
    for(std::size_t i=0;i<t.assemblies.size();++i) {
        const auto& a=t.assemblies[i];
        for(unsigned side=0;side<2;++side) {
            const auto b=a.bodies[side];surfaceSlots_[b]={i,side};surfaceOwners_[b]=a.owners[side];
            surfaceBodies_.emplace(a.surfaceId(side),b);
            // Door queries reserve both endpoint candidates at Reset. Filtering
            // current ownership avoids resizing/rebuilding indexes on actuation.
            for(unsigned endpoint=0;endpoint<2;++endpoint) {
                if(endpoint!=side && a.id.part!=StructuralPart::DoorLeaf) continue;
                if(const auto& o=a.owners[endpoint]) {
                    ownerSurfaces_[*o].push_back(b);
                    cavitySurfaces_[ownerCavities_.at(*o)].push_back(b);
                }
            }
        }
    }
    auto unique=[](auto& v) {std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());};
    for(auto& [owner,v]:ownerSurfaces_) unique(v);
    for(auto& v:cavitySurfaces_) unique(v);
    for(std::size_t i=0;i<t.contacts.size();++i) {
        const auto& c=t.contacts[i];bodyContacts_[c.a].push_back(i);bodyContacts_[c.b].push_back(i);
    }
    auto payload=[](const auto& v) {return v.capacity()*sizeof(typename std::decay_t<decltype(v)>::value_type);};
    auto& bytes=structure_.observationIndexPayloadBytes;
    bytes=payload(surfaceOwners_)+payload(surfaceSlots_)+payload(cavitySurfaces_)+payload(bodyContacts_)
        +payload(observedPortals_)+payload(cavityPortals_)+payload(committedIrradiance_);
    bytes+=ownerSurfaces_.size()*sizeof(decltype(ownerSurfaces_)::value_type)
        +ownerCavities_.size()*sizeof(decltype(ownerCavities_)::value_type)
        +ownerGas_.size()*sizeof(decltype(ownerGas_)::value_type)
        +surfaceBodies_.size()*sizeof(decltype(surfaceBodies_)::value_type);
    for(const auto& [o,v]:ownerSurfaces_) bytes+=payload(v);
    for(const auto& v:cavitySurfaces_) bytes+=payload(v);
    for(const auto& v:bodyContacts_) bytes+=payload(v);
    for(const auto& v:cavityPortals_) bytes+=payload(v);
}
StructuralSurfaceObservation CellAtmosphereTransport::surfaceObservation(std::size_t body,bool gasDetail) const {
    const auto [index,side]=surfaceSlots_[body];const auto& a=geometry_.structure->assemblies[index];
    const auto& b=structure_.bodies[body];
    StructuralSurfaceObservation result{a.surfaceId(side),surfaceOwners_[body],a.id.axis==2 ? (side==0 ? StructuralOrientation::Ceiling : StructuralOrientation::Floor) : StructuralOrientation::Side,
        a.areaM2,b.temperatureK(),b.capacityJPerK(),b.energyJ()};
    if(result.owner && gasDetail) {
        result.cavity=ownerCavities_.at(*result.owner);
        const auto gas=ownerGas_.find(*result.owner);
        if(gas!=ownerGas_.end()) {
            result.gasCell=gas->second;const auto& p=fluid_.primitives()[gas->second];
            result.gasCapacityJPerK=p.capacityJPerK;
            if(p.resolvedGas) {
                result.gasTemperatureK=p.temperatureK;
                // The solver uses the same current-mixture pressure bridge.
                constexpr std::array<double,4> molarMass{station::StationMolarMassOxygenKgPerMol,station::StationMolarMassNitrogenKgPerMol,
                    station::StationMolarMassCarbonDioxideKgPerMol,station::StationMolarMassWaterKgPerMol};
                std::array<double,4> fractions{};double total=0;
                for(unsigned g=0;g<4;++g) {fractions[g]=fluid_.states()[gas->second].q[g]/molarMass[g];total+=fractions[g];}
                for(auto& f:fractions) f/=total;
                result.gasContactWPerK=a.areaM2*station::thermalGasFilmWPerM2K(
                    {a.material.gasFilm.continuumWPerM2K,a.material.gasFilm.accommodation},p.pressurePa,
                    (p.temperatureK+b.temperatureK())/2,fractions);
            }
        }
    }
    return result;
}
StructuralQueryResult CellAtmosphereTransport::exteriorRadiation(
    std::span<ExteriorRadiationObservation> output,std::size_t offset) const {
    StructuralQueryResult result{0,exteriorSolar_.size()};
    if (offset>=result.total || output.empty()) return result;
    const double deepSpaceK=geometry_.thermal.hull ?
        geometry_.thermal.hull->sinkTemperatureC+station::StationCelsiusOffset : 3.0;
    const auto exposure=exposure_.evaluate(elapsed_);
    const double profile=solarCycleOverrides_ ?
        station::evaluateStationProfile(*geometry_.thermal.solarCycle,elapsed_) : 0.0;
    const double irradiance=geometry_.thermal.hull ?
        geometry_.thermal.hull->fullSunIrradianceKWPerM2*1000 : station::HullSettings{}.fullSunIrradianceKWPerM2*1000;
    constexpr double sigma=5.670374419e-8;
    for (std::size_t i=offset;i<result.total && result.written<output.size();++i) {
        const auto& face=exteriorSolar_[i];
        const auto [index,side]=surfaceSlots_[face.body];
        const auto& assembly=geometry_.structure->assemblies[index];
        const double area=face.areaM2*face.directScale; // Open leaves no longer face space.
        const double temperature=structure_.bodies[face.body].temperatureK();
        const double sink=solarCycleOverrides_ ? deepSpaceK : face.effectiveSinkTemperatureK(exposure_,deepSpaceK);
        const double absorbed=solarCycleOverrides_ ? face.lumpedProfileW(irradiance,profile) :
            face.directW(exposure_,exposure)+face.reflectedSunlightW(exposure_,exposure);
        const double outward=assembly.surfaceMaterial(side).emissivity*area*sigma*
            (std::pow(temperature,4)-std::pow(sink,4));
        output[result.written++]={assembly.surfaceId(side),area,temperature,absorbed,outward,absorbed-outward};
    }
    return result;
}
std::optional<StructuralSurfaceObservation> CellAtmosphereTransport::structuralSurface(StructuralSurfaceId id) const {
    const auto it=surfaceBodies_.find(id);
    return it==surfaceBodies_.end() ? std::nullopt : std::optional(surfaceObservation(it->second));
}
StructuralQueryResult CellAtmosphereTransport::querySurfaces(std::span<const std::size_t> candidates,
    std::optional<StructuralOwner> owner,std::optional<std::size_t> cavity,
    std::span<StructuralSurfaceObservation> output,std::size_t offset) const {
    StructuralQueryResult result;
    for(const auto b:candidates) {
        const auto& o=surfaceOwners_[b];
        if(!o || (owner && *owner!=*o) || (cavity && ownerCavities_.at(*o)!=*cavity)) continue;
        if(result.total++>=offset && result.written<output.size()) output[result.written++]=surfaceObservation(b);
    }
    return result;
}
StructuralSelectionSummary CellAtmosphereTransport::summarizeSurfaces(std::span<const std::size_t> candidates,
    std::optional<StructuralOwner> owner,std::optional<std::size_t> cavity) const {
    StructuralSelectionSummary result;
    auto add=[](StructuralSurfaceSummary& s,const StructuralSurfaceObservation& o) {
        if(s.surfaces++==0) s.minTemperatureK=s.maxTemperatureK=o.temperatureK;
        s.minTemperatureK=std::min(s.minTemperatureK,o.temperatureK);s.maxTemperatureK=std::max(s.maxTemperatureK,o.temperatureK);
        s.areaM2+=o.areaM2;s.capacityJPerK+=o.capacityJPerK;s.storedEnergyJ+=o.storedEnergyJ;
        s.areaTemperatureKm2+=o.areaM2*o.temperatureK;
    };
    for(const auto b:candidates) {
        const auto& o=surfaceOwners_[b];
        if(!o || (owner && *owner!=*o) || (cavity && ownerCavities_.at(*o)!=*cavity)) continue;
        const auto s=surfaceObservation(b,false);add(result.all,s);
        add(s.orientation==StructuralOrientation::Floor ? result.floor : s.orientation==StructuralOrientation::Ceiling ? result.ceiling : result.sides,s);
    }
    return result;
}
StructuralQueryResult CellAtmosphereTransport::structuralSurfaces(StructuralOwner o,std::span<StructuralSurfaceObservation> out,std::size_t offset) const {
    const auto it=ownerSurfaces_.find(o);return it==ownerSurfaces_.end() ? StructuralQueryResult{} : querySurfaces(it->second,o,{},out,offset);
}
StructuralQueryResult CellAtmosphereTransport::structuralSectorSurfaces(std::size_t c,std::span<StructuralSurfaceObservation> out,std::size_t offset) const {
    return c>=cavitySurfaces_.size() ? StructuralQueryResult{} : querySurfaces(cavitySurfaces_[c],{},c,out,offset);
}
StructuralSelectionSummary CellAtmosphereTransport::structuralSummary(StructuralOwner o) const {
    const auto it=ownerSurfaces_.find(o);return it==ownerSurfaces_.end() ? StructuralSelectionSummary{} : summarizeSurfaces(it->second,o,{});
}
StructuralSelectionSummary CellAtmosphereTransport::structuralSectorSummary(std::size_t c) const {
    return c>=cavitySurfaces_.size() ? StructuralSelectionSummary{} : summarizeSurfaces(cavitySurfaces_[c],{},c);
}
StructuralQueryResult CellAtmosphereTransport::structuralPortals(std::size_t cavity,std::span<StructuralPortalObservation> out,std::size_t offset) const {
    if(cavity>=cavityPortals_.size()) return {};
    const auto& indices=cavityPortals_[cavity];StructuralQueryResult result{0,indices.size()};
    for(std::size_t i=std::min(offset,indices.size());i<indices.size() && result.written<out.size();++i) {
        const auto index=indices[i];const auto& p=observedPortals_[index];
        StructuralPortalObservation o{index,p.a,p.b,p.areaM2,committedIrradiance_[p.a],
            p.b ? committedIrradiance_[*p.b] : std::optional<long double>(5.670374419e-8L*std::pow(static_cast<long double>(p.spaceTemperatureK),4)),{}};
        if(o.irradianceA && o.irradianceB) o.outwardPowerW=p.areaM2*(*o.irradianceA-*o.irradianceB);
        out[result.written++]=o;
    }
    return result;
}
StructuralQueryResult CellAtmosphereTransport::structuralContacts(StructuralSurfaceId id,std::span<StructuralContactObservation> out,std::size_t offset) const {
    const auto it=surfaceBodies_.find(id);if(it==surfaceBodies_.end()) return {};
    const auto& contacts=bodyContacts_[it->second];StructuralQueryResult result{0,contacts.size()};
    for(std::size_t i=std::min(offset,contacts.size());i<contacts.size() && result.written<out.size();++i) {
        const auto& c=geometry_.structure->contacts[contacts[i]];
        out[result.written++]={surfaceObservation(c.a,false).id,surfaceObservation(c.b,false).id,c.conductanceWPerK};
    }
    return result;
}
} // namespace nsi::systems
