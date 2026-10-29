#pragma once
#include <nsi/systems/Atmosphere.h>
#include <station/CellFluid.h>
#include <station/CellThermal.h>
#include <span>
#include <string>

namespace nsi::systems {
struct CellFluidDoor {
    std::size_t level{},wall{},face{SIZE_MAX};
    double fullOpenFraction{}; // Door clear area / full fluid face area
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
    std::vector<CellThermalContact> thermalContacts;
    // Level-local, row-major geometry -> active fluid Cell; missing is SIZE_MAX.
    std::vector<std::vector<std::size_t>> cellIndices;
};
// Authored geometry only; no simulation, no document mutations. Missing Walls
// facing geometric Void/outside breach; unmodelled Interior never becomes vacuum.
CellAtmosphereExpansion expandCellAtmosphere(const core::Installation&);

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
enum class CellThermalMode { Adiabatic, Geometry };
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
// Native gas/load layer, optionally coupled to the geometry Hull/partition mesh.
// Explicit reservoir references have no definitions in Installation expansion
// and refuse Geometry mode. No StationSystemsRuntime runs alongside this layer.
class CellAtmosphereTransport {
public:
    explicit CellAtmosphereTransport(CellAtmosphereExpansion,CellThermalMode=CellThermalMode::Adiabatic);
    explicit CellAtmosphereTransport(const core::Installation& installation,CellThermalMode mode=CellThermalMode::Adiabatic)
        :CellAtmosphereTransport(expandCellAtmosphere(installation),mode) {}
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
    double thermalFluxResidualJ() const {return thermalFluxResidual_;}
    // Whole transport call, not just the final acoustic/source slice.
    std::span<const station::CellFluidState> faceTransfers() const {return faceTransfers_;}
private:
    void sources(double seconds);
    void thermalStep(double seconds,double midpoint,std::size_t maxIterations);
    void publish();
    CellAtmosphereExpansion geometry_;
    station::CellFluidSolver fluid_;
    std::vector<station::CellFluidState> checkpoint_,faceTransfers_;
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
    std::string diagnostic_;
};
} // namespace nsi::systems
