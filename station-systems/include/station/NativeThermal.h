#pragma once
#include <station/ThermalBody.h>
#include <vector>
#include <optional>

namespace station {
// Inventory is supplied by the gas owner, never manufactured by this solver.
// Resolved gas has fixed species/cv/volume over one source interval; p = pPerK*T.
// Unresolved gas retains its energy but has no thermal contacts.
struct NativeThermalGas {
    double capacityJPerK{}, energyJ{}, pressurePaPerK{};
    std::array<double,4> wetMoleFractions{0,1,0,0};
    bool resolved{};
};
struct NativeThermalExterior {
    double spaceTemperatureK{3};
    // Already projected/absorbed W, from the existing solar profile. Do not
    // multiply by the full surface area again. One entry per exterior contact.
    double absorbedSolarW{};
};
struct NativeThermalBudget {
    std::size_t maxWork{100000}, maxNonlinearIterations{32}, maxLinearIterations{256}, maxRefinements{12};
    double maxIntervalSeconds{0.1};
};
struct NativeThermalReport {
    std::size_t work{}, intervals{}, refinements{}, nonlinearIterations{}, linearIterations{};
    // Max accepted residuals, plus the last nonlinear trial (also on refusal).
    double nonlinearResidualJ{}, lastTrialNonlinearResidualJ{}, fluxResidualJ{}, energyResidualJ{}, exteriorHeatJ{};
};
// Algebraic radiation extension: freeze a passive affine operator
// power = source - L*T at the supplied iterate. L must be symmetric positive
// semidefinite, with nonpositive off-diagonals. Affine sources may be signed
// (nonlinear radiation offsets); the converged physical boundary is passive.
// Nodes are bodies then gas, NEVER zero-capacity irradiance nodes. Implementations
// retain sparse cavity fields internally, allocate nothing and have no committed
// side effects. False refuses this attempt. Calls count toward whole-call work.
class ThermalAlgebraicOperator {
public:
    virtual ~ThermalAlgebraicOperator() = default;
    // Nested algebraic iterations share the solver's whole-call work counter.
    virtual void setWorkBudget(std::size_t*, std::size_t) {}
    // Checked after every matrix action; false refuses without publication.
    virtual bool healthy() const { return true; }
    // Optional independent boundary audit at the last frozen physical trial.
    virtual std::optional<long double> boundaryPowerW() const { return std::nullopt; }
    virtual bool freeze(std::span<const long double> temperatureK,
        std::span<long double> diagonal, std::span<long double> sourceW) = 0;
    virtual void apply(std::span<const long double> x, std::span<long double> Lx) const = 0;
};
// Opt-in numerical layer only. Does not change CellAtmosphereRuntime/standalone.
// solve() publishes temperatures/heat only on whole-call success; inputs and last
// successful outputs survive any refusal. Workspaces/topology allocate at Reset.
class NativeThermalSolver {
public:
    NativeThermalSolver(std::size_t bodies, std::size_t gasCells,
        std::vector<ThermalSolidContact>, std::vector<ThermalGasContact>,
        std::vector<ThermalRadiationContact>, std::size_t cavities=0);
    bool solve(std::span<const ThermalBody>, std::span<const NativeThermalGas>,
        std::span<const NativeThermalExterior>, double seconds,
        NativeThermalBudget = {}, ThermalAlgebraicOperator* = nullptr);
    // Reset-built slots only; actuation does not resize topology or touch energy.
    void setGasContact(std::size_t index, std::size_t gas, double areaM2);
    void setRadiationArea(std::size_t index, double areaM2);
    std::span<const double> heatJ() const { return heat_; }
    std::span<const double> temperatureK() const { return temperature_; }
    const NativeThermalReport& report() const { return report_; }
    std::string_view diagnostic() const { return diagnostic_; }
private:
    bool tick();
    bool freeze(ThermalAlgebraicOperator*);
    bool apply(std::span<const long double>, std::span<long double>, ThermalAlgebraicOperator*);
    bool interval(double, ThermalAlgebraicOperator*);
    std::size_t bodies_, gases_;
    std::vector<ThermalSolidContact> solid_;
    std::vector<ThermalGasContact> gas_;
    std::vector<ThermalRadiationContact> radiation_;
    std::span<const NativeThermalGas> inventory_;
    std::span<const NativeThermalExterior> exterior_;
    std::vector<long double> capacity_, current_, start_, guess_, conductance_, radiationG_,
        diagonal_, source_, opDiagonal_, opSource_, scratch_, rhs_, delta_, residual_, direction_, product_, z_, power_, totalHeat_;
    std::vector<double> heat_, temperature_;
    NativeThermalBudget budget_;
    NativeThermalReport report_;
    long double boundaryHeat_{};
    std::string_view diagnostic_;
};
} // namespace station
