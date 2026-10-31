#pragma once
#include <station/DiffuseRadiation.h>
#include <nsi/systems/Atmosphere.h>
#include <station/CellFluid.h>
#include <station/NativeThermal.h>
#include <nsi/systems/StructuralTopology.h>
#include <nsi/systems/StructuralObservations.h>
#include <nsi/systems/StellarExposure.h>
#include <array>
#include <functional>
#include <map>
#include <span>
#include <string>

namespace nsi::systems {
struct CellFluidDoor {
    std::size_t level{},wall{},face{SIZE_MAX};
    double fullOpenFraction{}; // Door clear area / full fluid face area
    StructuralAssemblyId structuralId{};
};
struct CellAtmosphereExpansion {
    station::CellFluidMesh mesh;
    std::vector<station::AtmosphereSettings> atmospheres;
    std::vector<CellFluidDoor> doors;
    ThermalExpansion thermal;
    std::optional<StructuralTopology> structure; // constructed at Reset for LocalStructure
    std::vector<StructuralOwner> gasOwners; // fluid index -> physical source Cell
    // Level-local, row-major geometry -> active fluid Cell; missing is SIZE_MAX.
    std::vector<std::vector<std::size_t>> cellIndices;
};
// Authored geometry only; no simulation, no document mutations. Missing Walls
// facing geometric Void/outside breach; unmodelled Interior never becomes vacuum.
CellAtmosphereExpansion expandCellAtmosphere(const core::Installation&,bool localStructure=true);

// One external source, at rest. Rate is integrated over simulated seconds;
// explicit inject() calls may instead supply a paused wall-time interval.
struct CellGasInjection {
    std::size_t cell{},gas{}; // native fluid Cell index; O2/N2/CO2/vapour
    double kgPerS{0.1},temperatureK{293.15};
};
struct CellAtmosphereLedger {
    std::array<double,4> sourceKg{},transportedKg{},exteriorLostKg{},leakedKg{};
    std::array<double,2> sourceMomentumKgMS{};
    double inventoryEnergyJ{},transportedEnergyJ{},externalHeatJ{},occupantHeatJ{},latentHeatJ{},conductedHeatJ{};
    double oxygenDemandKg{},oxygenConsumedKg{},co2ProducedKg{},vapourAddedKg{},condensedKg{},condensateEnergyJ{};
    bool operator==(const CellAtmosphereLedger&) const = default;
};
struct CellAtmosphereMomentum {
    std::array<double,2> starting{},current{},sources{},wallImpulse{},exteriorLost{}; // kg m/s
    bool operator==(const CellAtmosphereMomentum&) const = default;
};
struct CellSectorAtmosphere {
    std::array<double,4> massKg{};
    double volumeM3{},meanPressurePa{},minPressurePa{},maxPressurePa{};
    double temperatureK{},capacityJPerK{},totalEnergyJ{},kineticEnergyJ{};
    bool resolvedGas{};
    CellAtmosphereLedger ledger;
};
// Adiabatic is a gas-only numerical test/benchmark mode, not an Editor backend.
enum class CellThermalMode { Adiabatic, LocalStructure };
struct StructuralHeatRates {
    long double gasIntoSolidW{}, solidConductionMagnitudeW{}, interiorRadiationMagnitudeW{};
    long double portalEscapeW{}, exteriorRadiationW{}, absorbedSolarW{};
    bool operator==(const StructuralHeatRates&) const = default;
};
// One exterior Structural surface as the stellar model sees it. Precomputed
// once at Reset from the outward normal the structural topology already
// records as an assembly axis and side; never re-derived per step. World
// axes are the Nadir-lock body axes, so this is already the evaluator's
// frame: side 0 faces the negative axis, side 1 the positive.
struct ExteriorSolarFace {
    std::array<double, 3> outwardNormal{};
    double absorptivity{};
    double areaM2{};
    // The Structural surface body this face radiates to, so a caller can tie
    // a face back to its temperature.
    std::size_t body{SIZE_MAX};
    // Zeroed while a Door leaf is swung off this face, so an open Door
    // admits no direct stellar flux through it.
    double directScale{1.0};
    // Absorbed direct stellar power, W.
    double directW(const StellarExposureEvaluator& evaluator,
                  const StellarExposure& exposure) const noexcept {
        return evaluator.directFluxWPerM2(outwardNormal, absorptivity, exposure) * areaM2 * directScale;
    }
    // Absorbed power, W, from a hand-authored solar profile rather than
    // from the orbital model. A profile is written against the isotropic
    // absorptivity * area / 4 average it always had, not against a sun
    // direction this face may or may not be facing, because a hand-authored
    // profile replaces the sky outright instead of modulating one.
    // directScale is carried across unchanged from the pre-orbit lumped
    // path, where an open Door zeroed the face's whole solar weight: the
    // profile stands in for the collimated sun that Door used to block.
    double lumpedProfileW(double irradianceWPerM2, double level) const noexcept {
        return absorptivity * areaM2 * 0.25 * irradianceWPerM2 * level * directScale;
    }
    // Planet shine arriving on this face, W. Both components are applied
    // whatever the Door pose: directScale models the collimated direct beam
    // no longer being intercepted by a leaf swung out of the opening, while
    // Planet shine is a diffuse field the face both radiates into and
    // absorbs from. Gating one and not the other would break that balance.
    // The infrared figure is what *arrives*, not what is absorbed: a grey
    // body takes it with its emissivity, which the solver already applies
    // through the effective sink below, so it never enters absorbedSolarW.
    double planetInfraredW(const StellarExposureEvaluator& evaluator) const noexcept {
        return evaluator.planetInfraredFluxWPerM2(outwardNormal) * areaM2;
    }
    double reflectedSunlightW(const StellarExposureEvaluator& evaluator,
                             const StellarExposure& exposure) const noexcept {
        return evaluator.reflectedSunlightFluxWPerM2(outwardNormal, exposure) * absorptivity * areaM2;
    }
    // The Deep-space sink this face radiates toward, with the planet's
    // infrared folded in over the fraction of its view the planet fills.
    double effectiveSinkTemperatureK(const StellarExposureEvaluator& evaluator,
                                    double spaceTemperatureK) const noexcept {
        return evaluator.effectiveSinkTemperatureK(spaceTemperatureK,
                                                 evaluator.planetViewFactor(outwardNormal));
    }
};
struct CellStructuralThermalState {
    bool enabled{};
    std::string_view modelRevision{station::NativeThermalModelRevision};
    std::string_view presetRevision{core::NativeThermalPresetRevision};
    std::vector<station::ThermalBody> bodies; // same order/stable IDs as geometry.structure
    // Global audit: unique solids + gas internal/kinetic. Internal heat cancels;
    // inventory includes condensate carried energy; phase latent heat is separate.
    long double startingEnergyJ{},exteriorHeatJ{},currentEnergyJ{},residualJ{};
    long double solidEnergyJ{},gasInternalEnergyJ{},gasKineticEnergyJ{};
    station::NativeThermalReport work;
    station::NativeThermalPaths paths; // cumulative J
    StructuralHeatRates rates; // last committed whole-call mean W, zero at Reset
    double observedSeconds{};
    std::size_t assemblies{}, solidContacts{}, gasContacts{}, exteriorContacts{}, cavities{}, portals{}, topologyStorageBytes{};
    std::size_t observationIndexPayloadBytes{}; // vector capacities + map value payloads; excludes allocator/tree overhead
};
// Native gas/load layer coupled to LocalStructure; Adiabatic isolates gas tests.
// Explicit reservoir references have no definitions in Installation expansion
// and refuse thermal playback. No StationSystemsRuntime runs alongside this layer.
class CellAtmosphereTransport {
public:
    explicit CellAtmosphereTransport(CellAtmosphereExpansion,CellThermalMode=CellThermalMode::LocalStructure);
    explicit CellAtmosphereTransport(const core::Installation& installation,CellThermalMode mode=CellThermalMode::LocalStructure)
        :CellAtmosphereTransport(expandCellAtmosphere(installation,mode==CellThermalMode::LocalStructure),mode) {}
    // Source/phase splitting is bounded to 0.1 s, including accelerated updates.
    // At most 4096 source slices and maxSubsteps acoustic steps per whole call.
    // Optional host gate runs after numerical success but before publication
    // or clock commit. False/exception rolls back the native candidate. The
    // gate must not mutate this model; a true result must commit its own work.
    bool advance(double seconds,std::span<const double> heatJ={},std::size_t maxSubsteps=4096,std::size_t maxThermalIterations=4096,
        std::optional<CellGasInjection> injection=std::nullopt,const std::function<bool()>& accept={});
    // Atomic source-only control: no transport, phase/thermal solve or clock step.
    bool inject(const CellGasInjection&,double seconds);
    const station::CellFluidWork& work() const {return work_;}
    bool setDoorOpen(std::size_t level,std::size_t wall,bool open);
    const std::string& diagnostic() const {return diagnostic_;}
    double elapsedSeconds() const {return elapsed_;}
    const station::CellFluidSolver& fluid() const {return fluid_;}
    const CellAtmosphereExpansion& geometry() const {return geometry_;}
    std::span<const CellSectorAtmosphere> sectors() const {return sectors_;}
    const CellAtmosphereMomentum& momentum() const {return momentum_;}
    std::size_t thermalIterations() const {return thermalIterations_;}
    CellThermalMode thermalMode() const {return mode_;}
    const CellStructuralThermalState& structure() const {return structure_;}
    // Exterior Structural surfaces with their Reset-time outward normals, in
    // the same order as the solver's exterior contacts. Read-only view so
    // callers can assert lit-area geometry directly.
    std::span<const ExteriorSolarFace> exteriorSolarFaces() const noexcept {return exteriorSolar_;}
    // Orbital evaluator; dark without an Orbit or when a profile owns heating.
    const StellarExposureEvaluator& exposureEvaluator() const noexcept {return exposure_;}
    // True when a supplied solarCycle owns the whole stellar term, even
    // without an Orbit, and the evaluator was never built. It is genuinely
    // dark in this state, which is the structural proof that the orbital
    // model is suppressed rather than merely outweighed.
    bool solarCycleOverrides() const noexcept {return solarCycleOverrides_;}
    // The exposure-derived exterior boundary state staged for the solver:
    // one entry per exterior contact, in exterior contact order, holding the
    // effective Deep-space sink that face sees and the stellar power it
    // absorbs. This is the only state the orbital model writes per step, so
    // it is the state a refused step owes back. Read-only view so a caller
    // can assert the rollback contract directly instead of inferring it
    // from a downstream temperature, exactly as exteriorSolarFaces() exposes
    // the Reset-time normals for the same reason.
    std::span<const station::NativeThermalExterior> structuralExteriorState() const noexcept {
        return structuralExterior_;
    }
    // Caller-bounded, nonrecursive, allocation-free local queries. Cavity index
    // identifies a geometric Interior Sector, including missing-gas presets.
    StructuralQueryResult exteriorRadiation(std::span<ExteriorRadiationObservation>, std::size_t offset=0) const;
    std::optional<StructuralSurfaceObservation> structuralSurface(StructuralSurfaceId) const;
    StructuralQueryResult structuralSurfaces(StructuralOwner, std::span<StructuralSurfaceObservation>, std::size_t offset=0) const;
    StructuralQueryResult structuralSectorSurfaces(std::size_t cavity, std::span<StructuralSurfaceObservation>, std::size_t offset=0) const;
    StructuralSelectionSummary structuralSummary(StructuralOwner) const;
    StructuralSelectionSummary structuralSectorSummary(std::size_t cavity) const;
    StructuralQueryResult structuralPortals(std::size_t cavity, std::span<StructuralPortalObservation>, std::size_t offset=0) const;
    StructuralQueryResult structuralContacts(StructuralSurfaceId, std::span<StructuralContactObservation>, std::size_t offset=0) const;
    double thermalFluxResidualJ() const {return thermalFluxResidual_;}
    // Whole transport call, not just the final acoustic/source slice.
    std::span<const station::CellFluidState> faceTransfers() const {return faceTransfers_;}
private:
    void buildStructuralObservations();
    StructuralSurfaceObservation surfaceObservation(std::size_t, bool gasDetail=true) const;
    StructuralQueryResult querySurfaces(std::span<const std::size_t>, std::optional<StructuralOwner>, std::optional<std::size_t>, std::span<StructuralSurfaceObservation>, std::size_t) const;
    StructuralSelectionSummary summarizeSurfaces(std::span<const std::size_t>, std::optional<StructuralOwner>, std::optional<std::size_t>) const;
    std::vector<station::ThermalRadiationPortal> observedPortals_;
    std::vector<std::vector<std::size_t>> cavityPortals_;
    std::vector<std::optional<long double>> committedIrradiance_;
    std::vector<std::optional<StructuralOwner>> surfaceOwners_;
    std::map<StructuralOwner,std::vector<std::size_t>> ownerSurfaces_;
    std::map<StructuralOwner,std::size_t> ownerCavities_, ownerGas_;
    std::vector<std::vector<std::size_t>> cavitySurfaces_, bodyContacts_;
    std::map<StructuralSurfaceId,std::size_t> surfaceBodies_;
    std::vector<std::pair<std::size_t,unsigned>> surfaceSlots_; // assembly/side in body order
    bool validInjection(const CellGasInjection&,double seconds) const;
    void applyInjection(const CellGasInjection&,double seconds);
    void sources(double seconds);
    void publish();
    void structuralStep(double seconds,double midpoint,std::size_t maxWork);
    CellAtmosphereExpansion geometry_;
    station::CellFluidSolver fluid_;
    std::vector<station::CellFluidState> checkpoint_,faceTransfers_,checkpointTransfers_;
    std::vector<CellAtmosphereLedger> ledger_,checkpointLedger_;
    std::vector<CellSectorAtmosphere> sectors_;
    std::vector<std::size_t> sourceCells_;
    std::vector<double> sourceWeights_;
    double elapsed_{};
    station::CellFluidWork work_;
    CellAtmosphereMomentum momentum_;
    std::size_t thermalIterations_{};
    double thermalFluxResidual_{};
    CellThermalMode mode_;
    CellStructuralThermalState structure_;
    std::optional<station::NativeThermalSolver> structuralSolver_;
    std::optional<station::DiffuseRadiation> structuralRadiation_;
    std::vector<station::ThermalBody> checkpointBodies_;
    // The last committed exposure-derived exterior boundary state, so a
    // refused step hands back the sky it last actually stood under rather
    // than the sky of the attempt it just threw away.
    std::vector<station::NativeThermalExterior> exteriorCheckpoint_;
    std::vector<station::NativeThermalGas> structuralGas_;
    std::vector<station::NativeThermalExterior> structuralExterior_;
    std::vector<ExteriorSolarFace> exteriorSolar_;
    StellarExposureEvaluator exposure_;
    bool solarCycleOverrides_{};
    struct DoorPose {
        std::size_t door{},portal{},parkCavity{};
        StructuralOwner parkOwner{};
        std::array<std::size_t,2> bodies{SIZE_MAX,SIZE_MAX};
        std::array<std::optional<StructuralOwner>,2> closedOwners;
        std::optional<std::size_t> parkGas;
        std::array<std::size_t,2> surface{SIZE_MAX,SIZE_MAX},exterior{SIZE_MAX,SIZE_MAX},gas{SIZE_MAX,SIZE_MAX};
        std::array<std::optional<std::size_t>,2> closedCavity,closedGas;
    };
    std::vector<DoorPose> doorPoses_;
    std::string diagnostic_;
};
} // namespace nsi::systems
