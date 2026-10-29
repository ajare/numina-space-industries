#pragma once
#include "SimulationStepTiming.h"
#include <nsi/systems/CellAtmosphere.h>
#include <station/StationSystems.h>
#include <station/CellAerodynamics.h>
#include <station/StationLifecycleRuntime.h>

namespace station::editor {
// Editor-only facade: the native model is the sole gas/heat owner. Shared
// observation value types keep panels independent of the numerical backend.
// Four lifecycle sandboxes gate each host update; no standalone C++ ledger runs.
class CellPlaybackRuntime {
public:
    explicit CellPlaybackRuntime(const nsi::core::Installation&,CellObservationOptions = {},StationSystemsSource = std::nullopt,
        nsi::systems::CellThermalMode = nsi::systems::CellThermalMode::Geometry);
    void setObservationOptions(CellObservationOptions);
    CellObservationOptions observationOptions() const {return options_;}
    // Cell-centre one-way load, not rigid-body movement. Disabled, absent or
    // unresolved samples return nullopt; force sampling does not require wind diagnostics.
    std::optional<CellAerodynamicLoad> objectForce(std::size_t level,std::size_t gridIndex,const CellAerodynamicBody& = {}) const;
    bool update(double seconds,std::size_t acousticBudget=4096,std::size_t thermalBudget=4096);
    bool setDoorOpen(std::size_t level,std::size_t wall,bool open);
    const StationSystemsSnapshot& snapshot() const {return snapshot_;}
    // Committed solid observations, separate from gas and legacy Hull snapshots.
    const nsi::systems::CellStructuralThermalState& structuralSnapshot() const {return model_.structure();}
    const std::vector<StationSystemsDiagnostic>& diagnostics() const {return diagnostics_;}
    double averageStepMicroseconds() const {return timing_.averageMicroseconds();}
    const SimulationStepTiming& stepTimings() const {return timing_;}
    const nsi::systems::CellAtmosphereTransport& model() const {return model_;}
private:
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
