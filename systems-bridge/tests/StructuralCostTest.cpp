#include "StructuralCostModel.h"
#include <cstdlib>
#include <iostream>
#include <new>
namespace {bool tracking=false;std::size_t allocations=0;}
void* operator new(std::size_t n) {if (tracking) ++allocations;if (void* p=std::malloc(n ? n : 1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace structural_cost;
int main() {
    try {
        for (const std::string name:{"rest","many-sectors","tall-offset","breaches"}) {
            const auto scene=fixture(name,16);Model m(scene,2),vacuum(scene,2,true);
            require(m.panels==vacuum.panels && m.localLinks==vacuum.localLinks,"evacuation removed structure");
            require(m.bodies.size()==m.gasNodes+2*m.panels,"two-side body count mismatch");
            require(m.surfaces.size()==2*m.panels && m.throughLinks==m.panels,"panel mass/side deduplication mismatch");
            if (name=="rest") require(m.panels==576 && m.cavities.size()==1,"flat enclosure count mismatch");
            if (name=="many-sectors") require(m.cavities.size()==8 && m.panels>576,"partition count missing");
            if (name=="tall-offset") require(m.panels==600 && m.cavities.size()==2,"tall/offset deduplication count mismatch");
            const double energy=vacuum.energy();tracking=true;vacuum.advance(1);tracking=false;
            require(allocations==0,"prototype numerical update allocated");
            require(vacuum.energy()<energy,"evacuated solids did not radiate");
            require(vacuum.work.intervals==10 && vacuum.work.residualJ<1e-5 && vacuum.work.energyResidualJ<1e-4,"work/residual check failed");
            Model gradient(scene,2,false,true),refined=gradient;
            gradient.advance(0.1);for (int i=0;i<10;++i) refined.advance(0.01);
            double error=0;for (std::size_t i=0;i<gradient.bodies.size();++i)
                error=std::max(error,std::abs(gradient.bodies[i].temperature-refined.bodies[i].temperature));
            require(error<0.002,"P1 temporal refinement error exceeded study envelope");
        }
        // A closed uniform cavity has zero exchange, independent of gas.
        Model closed(fixture("rest",16),2,true);
        for (auto& s:closed.surfaces) if (s.cavity==SIZE_MAX) s.epsilon=0;
        const auto before=closed.bodies;closed.advance(0.1);
        for (std::size_t i=0;i<before.size();++i) require(std::abs(before[i].temperature-closed.bodies[i].temperature)<1e-10,"closed cavity invented heat");
        // Small aperture is aperture-limited, not whole-interior direct exposure.
        closed.cavities[0].aperture=1.8;const double energy=closed.energy();closed.advance(0.1);
        require(energy-closed.energy()>0 && energy-closed.energy()<=0.1*1.8*sigma*std::pow(293.15,4)+1e-5,"black aperture bound exceeded");
        auto none=fixture("rest",16);none.setHullType(std::nullopt);Model absent(none,2);
        require(absent.bodies.empty(),"hull:none created structure");
        std::cout<<"P1 counts, passivity, vacuum, cavity/aperture, refinement and no-allocation checks passed\n";
    } catch (const std::exception& e) {tracking=false;std::cerr<<e.what()<<'\n';return 1;}
}
