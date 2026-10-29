#pragma once
#include <station/NativeThermal.h>
#include <optional>

namespace station {
struct ThermalRadiationPortal {
    std::size_t a{};
    std::optional<std::size_t> b; // absent = geometric exterior, not absent gas
    double areaM2{};
    double spaceTemperatureK{3};
};
// Visibility-free, well-mixed diffuse Sector fields. No stored cavity energy.
// Topology is fixed at construction; all numerical scratch is preallocated.
class DiffuseRadiation final : public ThermalAlgebraicOperator {
public:
    DiffuseRadiation(std::size_t nodes, std::size_t cavities,
        std::vector<ThermalRadiationContact>, std::vector<ThermalRadiationPortal>);
    void setSurface(std::size_t index, std::size_t cavity, double areaM2);
    void setPortalArea(std::size_t index, double areaM2);
    // Recompute sparse coefficients after a batch of control changes; no allocation.
    void updateCoefficients();
    void setWorkBudget(std::size_t*, std::size_t) override;
    bool healthy() const override { return healthy_; }
    std::optional<long double> boundaryPowerW() const override;
    long double trialPortalPowerW(std::size_t portal) const;
    bool freeze(std::span<const long double>, std::span<long double>, std::span<long double>) override;
    void apply(std::span<const long double>, std::span<long double>) const override;
    // Trial diagnostics only, not committed playback observations. Nullopt means
    // the entire connected component has neither absorption nor boundary exchange.
    std::optional<long double> trialIrradiance(std::size_t cavity) const;
private:
    bool tick() const;
    bool solveField() const;
    void matrix(std::span<const long double>, std::span<long double>) const;
    std::size_t nodes_;
    std::vector<ThermalRadiationContact> surfaces_;
    std::vector<ThermalRadiationPortal> portals_;
    std::vector<long double> diagonal_, boundary_, offset_;
    std::vector<bool> active_,anchored_;
    std::vector<std::size_t> parent_;
    mutable std::vector<long double> rhs_, field_, residual_, direction_, product_, z_;
    std::vector<long double> trial_;
    long double slope_{};
    std::size_t* work_{};
    std::size_t limit_{};
    mutable bool healthy_{true};
    bool trialValid_{};
};
} // namespace station
