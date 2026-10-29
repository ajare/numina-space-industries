#pragma once
#include <nsi/core/Installation.h>
#include <string_view>

namespace nsi::core {
// Physical parameter revision, NOT a YAML schema or approval of default rollout.
inline constexpr std::string_view NativeThermalPresetRevision = "C1-v1-provisional";
struct ThermalGasFilmSpec {
    double continuumWPerM2K{20};
    // Symmetric effective accommodation alpha/(2-alpha), not a one-wall fit.
    double accommodation{0.8};
};
struct ThermalSurfaceSpec {
    double emissivity{0.8};
    double absorptivity{0.4};
};
struct ThermalAssemblySpec {
    double arealCapacityJPerM2K{}; // total physical assembly, not per side
    double firstSideCapacityFraction{0.5}; // other side receives 1-f
    double throughResistanceM2KPerW{}; // solid only; excludes films/radiation
    ThermalSurfaceSpec firstSide{}, secondSide{}; // Hull: inside, outside
    double sheetConductanceWPerK{}; // total k*d, W/K per square; half per side
    double junctionConductanceWPerMK{}; // total per seam length; half per side
    ThermalGasFilmSpec gasFilm{};
};
// Throw invalid_argument for nonfinite/nonpassive inputs. SI units throughout.
void validate(const ThermalGasFilmSpec&);
void validate(const ThermalAssemblySpec&);
// Opt-in candidate descriptions. Legacy hullSpec/partitionSpec stay unchanged.
// Doors use their parent assembly, with fixed + leaf area counted exactly once.
ThermalAssemblySpec nativeThermalSpec(Installation::HullType);
ThermalAssemblySpec nativeThermalSpec(Installation::PartitionType);
} // namespace nsi::core
