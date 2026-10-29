#pragma once
#include <station/Configuration.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace station {
struct CellThermalLink {
    std::size_t a{},b{};
    double conductanceWPerK{};
};
// Sparse backward Euler (C + dt L), exact elimination for star networks and
// Jacobi-preconditioned conjugate gradients otherwise. Capacities J/K and temperatures K. Zero capacity detaches a node;
// it never supplies hidden thermal mass or mediates heat through empty space.
// All buffers are preallocated. Failed solves publish no transfers.
class CellThermalSolver {
public:
    CellThermalSolver(std::size_t nodes,std::vector<CellThermalLink>);
    bool solve(std::span<const long double> capacity,std::span<const long double> temperature,
        double seconds,std::size_t maxIterations=256);
    std::span<const double> heatJ() const {return heat_;}
    std::span<const double> transferredJ() const {return transferred_;} // a -> b
    const std::string& diagnostic() const {return diagnostic_;}
    std::size_t iterations() const {return iterations_;}
    // Enforce zero summed heat on the largest-capacity body, within the
    // convergence budget. These diagnostics make roundoff/flux residuals visible.
    double energyCorrectionJ() const {return correction_;}
    double maxFluxResidualJ() const {return fluxResidual_;}
private:
    std::vector<CellThermalLink> links_;
    std::vector<long double> diagonal_,rhs_,delta_,residual_,direction_,product_,z_,nodeFlux_;
    std::vector<double> heat_,transferred_;
    std::size_t iterations_{},starHub_{SIZE_MAX};
    double correction_{},fluxResidual_{};
    std::string diagnostic_;
};
struct CellHullRadiation {
    long double temperatureChangeK{};
    double absorbedJ{},emittedJ{}; // signed net IR exchange, including back radiation
};
// Same adaptive nonlinear radiation integration as the compartment Hull.
std::optional<CellHullRadiation> integrateCellHullRadiation(const HullSettings&,
    long double temperatureK,double absorbedKW,double seconds);
} // namespace station
