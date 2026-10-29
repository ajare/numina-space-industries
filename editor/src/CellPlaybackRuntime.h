#pragma once
#include <nsi/systems/CellAtmosphere.h>
#include <station/StationSystems.h>
#include <station/CellAerodynamics.h>

namespace station::editor {
// Editor-only facade: the native model is the sole gas/heat owner. Shared
// observation value types keep panels independent of the numerical backend.
class CellPlaybackRuntime {
public:
    explicit CellPlaybackRuntime(const nsi::core::Installation&,CellObservationOptions = {});
    void setObservationOptions(CellObservationOptions);
    CellObservationOptions observationOptions() const {return options_;}
    // Cell-centre one-way load, not rigid-body movement. Disabled, absent or
    // unresolved samples return nullopt; force sampling does not require wind diagnostics.
    std::optional<CellAerodynamicLoad> objectForce(std::size_t level,std::size_t gridIndex,const CellAerodynamicBody& = {}) const;
    bool update(double seconds,std::size_t acousticBudget=4096,std::size_t thermalBudget=4096);
    bool setDoorOpen(std::size_t level,std::size_t wall,bool open);
    const StationSystemsSnapshot& snapshot() const {return snapshot_;}
    const std::vector<StationSystemsDiagnostic>& diagnostics() const {return diagnostics_;}
    double averageStepMicroseconds() const {return steps_ ? totalMicros_/steps_ : 0;}
    const nsi::systems::CellAtmosphereTransport& model() const {return model_;}
private:
    void publish(double seconds);
    void doorConditions();
    void publishDiagnostics();
    nsi::systems::CellAtmosphereTransport model_;
    CellAerodynamicField aerodynamics_;
    CellObservationOptions options_;
    StationSystemsSnapshot snapshot_;
    std::vector<nsi::systems::CellAtmosphereLedger> previous_;
    std::vector<StationSystemsDiagnostic> diagnostics_;
    struct PathFace {std::size_t face;double sign;};
    std::vector<PathFace> paths_;
    double totalMicros_{};
    std::size_t steps_{};
};
} // namespace station::editor
