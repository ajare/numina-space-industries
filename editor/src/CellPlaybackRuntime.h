#pragma once
#include "SimulationStepTiming.h"
#include <nsi/systems/CellAtmosphere.h>
#include <station/StationSystems.h>
#include <station/CellAerodynamics.h>
#include <station/StationLifecycleRuntime.h>

namespace station::editor {
struct GasInjector {
    std::size_t level{},gridIndex{},gas{}; // geometric Cell, not a Sector-wide source
    double kgPerS{0.1}; // 20 C gas, injected at rest
};
// Editor-only facade: the native model is the sole gas/heat owner. Shared
// observation value types keep panels independent of the numerical backend.
// Four lifecycle sandboxes gate each host update; no standalone C++ ledger runs.
class CellPlaybackRuntime {
public:
    explicit CellPlaybackRuntime(const nsi::core::Installation&,CellObservationOptions = {},StationSystemsSource = std::nullopt);
    // Reset-time explicit expansion for controlled initial-state experiments.
    // The expansion must correspond to installation; no mid-run topology swap.
    CellPlaybackRuntime(const nsi::core::Installation&,nsi::systems::CellAtmosphereExpansion,
        CellObservationOptions,StationSystemsSource);
    void setObservationOptions(CellObservationOptions);
    CellObservationOptions observationOptions() const {return options_;}
    // Cell-centre one-way load, not rigid-body movement. Disabled, absent or
    // unresolved samples return nullopt; force sampling does not require wind diagnostics.
    std::optional<CellAerodynamicLoad> objectForce(std::size_t level,std::size_t gridIndex,const CellAerodynamicBody& = {}) const;
    bool update(double seconds,std::size_t acousticBudget=4096,std::size_t thermalBudget=4096,
        std::optional<GasInjector> injection=std::nullopt);
    bool canInject(const GasInjector& source) const {return resolveInjector(source).has_value();}
    bool inject(const GasInjector&,double seconds); // paused source-only operation
    bool setDoorOpen(std::size_t level,std::size_t wall,bool open);
    const StationSystemsSnapshot& snapshot() const {return snapshot_;}
    // Committed solid observations, separate from gas and legacy Hull snapshots.
    const nsi::systems::CellStructuralThermalState& structuralSnapshot() const {return model_.structure();}
    // Local inspection is pull-based and caller bounded, never a recursive copy
    // into StationSystemsSnapshot. Geometric cavity indices include absent gas.
    nsi::systems::StructuralSelectionSummary structuralCell(nsi::systems::StructuralOwner owner) const {return model_.structuralSummary(owner);}
    nsi::systems::StructuralSelectionSummary structuralSector(std::size_t cavity) const {return model_.structuralSectorSummary(cavity);}
    nsi::systems::StructuralQueryResult structuralSurfaces(nsi::systems::StructuralOwner owner,
        std::span<nsi::systems::StructuralSurfaceObservation> out,std::size_t offset=0) const {return model_.structuralSurfaces(owner,out,offset);}
    const std::vector<StationSystemsDiagnostic>& diagnostics() const {return diagnostics_;}
    double averageStepMicroseconds() const {return timing_.averageMicroseconds();}
    const SimulationStepTiming& stepTimings() const {return timing_;}
    const nsi::systems::CellAtmosphereTransport& model() const {return model_;}
private:
    std::optional<nsi::systems::CellGasInjection> resolveInjector(const GasInjector&) const;
    void publish(double seconds);
    void doorConditions();
    void publishDiagnostics();
    nsi::systems::CellAtmosphereTransport model_;
    StationLifecycleRuntime lifecycle_;
    CellAerodynamicField aerodynamics_;
    CellObservationOptions options_;
    StationSystemsSnapshot snapshot_;
    std::vector<nsi::systems::CellAtmosphereLedger> previous_;
    std::vector<StationSystemsDiagnostic> diagnostics_;
    struct PathFace {std::size_t face;double sign;};
    std::vector<PathFace> paths_;
    SimulationStepTiming timing_;
};
} // namespace station::editor
