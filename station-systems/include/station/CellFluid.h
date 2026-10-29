#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace station {
inline constexpr std::string_view CellFluidModelRevision="cell-fluid-v2-shear";
// Integrated conserved quantities in SI: O2/N2/CO2/vapour kg, x/y momentum kg m/s,
// total (internal + kinetic) energy J. No script/runtime/geometry dependency.
struct CellFluidState {
    std::array<double,7> q{};
};
struct CellFluidCell {
    double volumeM3{12};
    std::size_t level{}, gridIndex{}, atmosphere{};
    CellFluidState initial;
};
struct CellFluidFace {
    std::size_t a{}, b{SIZE_MAX}; // missing b: exterior vacuum
    int axis{}, sign{1}; // normal from a to b, or outward at the boundary
    double areaM2{6}, openFraction{1}, dischargeCoefficient{1}; // full face, fraction, effective-area factor
    std::size_t pathway{SIZE_MAX};
};
struct CellFluidMesh {
    std::vector<CellFluidCell> cells;
    std::vector<CellFluidFace> faces;
};
struct CellFluidPrimitive {
    double massKg{}, density{}, pressurePa{}, temperatureK{}, velocityX{}, velocityY{}, soundSpeed{}, capacityJPerK{};
    bool resolvedGas{}; // false: temperature/velocity are unavailable (zero placeholders)
};
struct CellFluidWork {
    std::size_t substeps{}, faceEvaluations{}, retries{};
    bool resting{};
};
CellFluidState makeCellFluidState(const std::array<double,4>& mass,double temperatureK,
    double velocityX=0,double velocityY=0);
CellFluidPrimitive cellFluidPrimitive(const CellFluidState&,double volumeM3);

// First-order positivity-checked HLLE finite volumes with contact-speed shear
// restoration and adaptive acoustic CFL. Bulk/species mixing remains HLLE.
// All stepping buffers are allocated at construction. advance is transactional;
// a deterministic work limit refuses the update instead of skipping its time.
class CellFluidSolver {
public:
    explicit CellFluidSolver(CellFluidMesh mesh);
    bool advance(double seconds,std::size_t maxSubsteps=4096);
    const std::string& diagnostic() const { return diagnostic_; }
    const CellFluidWork& work() const { return work_; }
    const CellFluidMesh& mesh() const { return mesh_; }
    std::span<const CellFluidState> states() const { return state_; }
    std::span<const CellFluidPrimitive> primitives() const { return primitive_; }
    // Last advance's open-face fluxes, signed a -> b, in integrated SI units.
    // Momentum includes pressure stress, even at rest; energy includes pressure work.
    std::span<const CellFluidState> faceTransfers() const { return transferred_; }
    std::array<double,2> wallImpulse() const { return wallImpulse_; }
    void setOpening(std::size_t face,double fraction);
    void restore(std::span<const CellFluidState>);
    // Sources update the primitive cache immediately. Additions are at rest;
    // removals carry local velocity and their internal + kinetic energy.
    double addSpecies(std::size_t cell,std::size_t gas,double kg,double temperatureK);
    struct Removal { double kg{}, energyJ{}; };
    Removal removeSpecies(std::size_t cell,std::size_t gas,double kg);
    void addHeat(std::size_t cell,double joules);
    struct Condensation { double kg{}, latentJ{}, carriedEnergyJ{}; };
    // Condensate leaves gas with its local momentum and sensible/kinetic energy;
    // latent heat is returned to the remaining gas. Atomic on refusal.
    Condensation condenseVapour(std::size_t cell);
private:
    void refresh();
    void rebaseReference();
    CellFluidMesh mesh_;
    std::vector<CellFluidState> state_,backup_,candidate_,delta_,rate_,transferred_;
    std::vector<CellFluidPrimitive> primitive_;
    std::vector<unsigned char> active_,initialContacts_;
    std::vector<double> inverseVolume_,sqrtDensity_; // exact primitive-derived Roe weights, preallocated
    std::vector<std::size_t> referenceOrder_;
    double referencePressure_{};
    std::array<double,4> referenceDensity_{};
    std::array<double,2> areaRate_{};
    CellFluidWork work_;
    std::array<double,2> wallImpulse_{};
    std::string diagnostic_;
};
} // namespace station
