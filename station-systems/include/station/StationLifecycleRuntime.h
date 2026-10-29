#pragma once
#include "station/StationSystems.h"
#include <memory>

namespace station {
// Only the four sandboxed lifecycle/mirror objects: no native plant ledger,
// compartment solver or authoritative inventory. Requires enabled settings.
// prepare runs once per host update; commit acknowledges it only after physical
// success. A prepared same-duration retry is reused, never executed twice.
class StationLifecycleRuntime {
public:
    explicit StationLifecycleRuntime(StationSystemsSettings,StationSystemsSource = std::nullopt);
    ~StationLifecycleRuntime();
    StationLifecycleRuntime(const StationLifecycleRuntime&)=delete;
    StationLifecycleRuntime& operator=(const StationLifecycleRuntime&)=delete;
    // Invalid input or script failure leaves committed counters/time unchanged.
    // Changing a prepared duration or recovering partially failed Lua requires
    // deterministic reconstruction from committed durations, bounded explicitly.
    bool prepare(double seconds,std::size_t maxRecoverySteps=4096);
    void commit() noexcept; // requires successful prepare; no allocation
    uint64_t completedSteps() const;
    double elapsedSeconds() const;
    std::array<uint64_t,4> completedTicks() const;
    const std::vector<StationSystemsDiagnostic>& diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace station
