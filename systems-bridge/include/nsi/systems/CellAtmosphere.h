#pragma once
#include <station/DiffuseRadiation.h>
#include <nsi/systems/Atmosphere.h>
#include <station/CellFluid.h>
#include <station/CellThermal.h>
#include <station/NativeThermal.h>
#include <nsi/systems/StructuralTopology.h>
#include <nsi/systems/StructuralObservations.h>
#include <map>
#include <span>
#include <string>

namespace nsi::systems {
struct CellFluidDoor {
    std::size_t level{},wall{},face{SIZE_MAX};
    double fullOpenFraction{}; // Door clear area / full fluid face area
    StructuralAssemblyId structuralId{};
};
struct CellThermalContact {
    std::size_t a{},b{SIZE_MAX},link{}; // b missing: singleton Hull
    double areaM2{},conductanceKWPerK{};
};
struct CellAtmosphereExpansion {
    station::CellFluidMesh mesh;
    std::vector<station::AtmosphereSettings> atmospheres;
    std::vector<CellFluidDoor> doors;
    ThermalExpansion thermal;
    std::optional<StructuralTopology> structure; // constructed only for the Reset-selected candidate
    std::vector<StructuralOwner> gasOwners; // fluid index -> physical source Cell
    std::vector<CellThermalContact> thermalContacts;
    // Level-local, row-major geometry -> active fluid Cell; missing is SIZE_MAX.
    std::vector<std::vector<std::size_t>> cellIndices;
};
// Authored geometry only; no simulation, no document mutations. Missing Walls
// facing geometric Void/outside breach; unmodelled Interior never becomes vacuum.
CellAtmosphereExpansion expandCellAtmosphere(const core::Installation&,bool localStructure=false);

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
enum class CellThermalMode { Adiabatic, Geometry, LocalStructure };
struct StructuralHeatRates {
    long double gasIntoSolidW{}, solidConductionMagnitudeW{}, interiorRadiationMagnitudeW{};
    long double portalEscapeW{}, exteriorRadiationW{}, absorbedSolarW{};
    bool operator==(const StructuralHeatRates&) const = default;
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
struct CellHullThermalState {
    long double temperatureK{};
    double capacityJPerK{},startingEnergyJ{},absorbedJ{},emittedJ{},conductedJ{};
    long double energyJ() const {return capacityJPerK*temperatureK;}
    bool operator==(const CellHullThermalState&) const = default;
};
struct CellAtmosphereThermalState {
    std::optional<CellHullThermalState> hull;
    std::vector<double> linkEnergyJ;
};
// Native gas/load layer, optionally coupled to exactly one geometry heat backend.
// Explicit reservoir references have no definitions in Installation expansion
// and refuse Geometry mode. No StationSystemsRuntime runs alongside this layer.
class CellAtmosphereTransport {
public:
    explicit CellAtmosphereTransport(CellAtmosphereExpansion,CellThermalMode=CellThermalMode::Adiabatic);
    explicit CellAtmosphereTransport(const core::Installation& installation,CellThermalMode mode=CellThermalMode::Adiabatic)
        :CellAtmosphereTransport(expandCellAtmosphere(installation,mode==CellThermalMode::LocalStructure),mode) {}
    // Source/phase splitting is bounded to 0.1 s, including accelerated updates.
    // At most 4096 source slices and maxSubsteps acoustic steps per whole call.
    bool advance(double seconds,std::span<const double> heatJ={},std::size_t maxSubsteps=4096,std::size_t maxThermalIterations=4096);
    const station::CellFluidWork& work() const {return work_;}
    bool setDoorOpen(std::size_t level,std::size_t wall,bool open);
    const std::string& diagnostic() const {return diagnostic_;}
    double elapsedSeconds() const {return elapsed_;}
    const station::CellFluidSolver& fluid() const {return fluid_;}
    const CellAtmosphereExpansion& geometry() const {return geometry_;}
    std::span<const CellSectorAtmosphere> sectors() const {return sectors_;}
    const CellAtmosphereMomentum& momentum() const {return momentum_;}
    const CellAtmosphereThermalState& thermal() const {return thermal_;}
    std::size_t thermalIterations() const {return thermalIterations_;}
    CellThermalMode thermalMode() const {return mode_;}
    const CellStructuralThermalState& structure() const {return structure_;}
    // Caller-bounded, nonrecursive, allocation-free local queries. Cavity index
    // identifies a geometric Interior Sector, including missing-gas presets.
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
    void sources(double seconds);
    void thermalStep(double seconds,double midpoint,std::size_t maxIterations);
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
    CellAtmosphereThermalState thermal_;
    std::optional<station::CellThermalSolver> thermalSolver_;
    std::vector<long double> thermalCapacity_,thermalTemperature_;
    std::vector<double> checkpointThermalLinks_;
    std::size_t thermalIterations_{};
    double thermalFluxResidual_{};
    CellThermalMode mode_;
    CellStructuralThermalState structure_;
    std::optional<station::NativeThermalSolver> structuralSolver_;
    std::optional<station::DiffuseRadiation> structuralRadiation_;
    std::vector<station::ThermalBody> checkpointBodies_;
    std::vector<station::NativeThermalGas> structuralGas_;
    std::vector<station::NativeThermalExterior> structuralExterior_;
    std::vector<double> solarWeights_;
    struct DoorPose {
        std::size_t door{},portal{},parkCavity{};
        StructuralOwner parkOwner{};
        std::array<std::size_t,2> bodies{SIZE_MAX,SIZE_MAX};
        std::array<std::optional<StructuralOwner>,2> closedOwners;
        std::optional<std::size_t> parkGas;
        std::array<std::size_t,2> surface{SIZE_MAX,SIZE_MAX},exterior{SIZE_MAX,SIZE_MAX},gas{SIZE_MAX,SIZE_MAX};
        std::array<std::optional<std::size_t>,2> closedCavity,closedGas;
        std::array<double,2> solar{};
    };
    std::vector<DoorPose> doorPoses_;
    std::string diagnostic_;
};
} // namespace nsi::systems
