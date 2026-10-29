#pragma once
#include "station/CellFluid.h"
#include "station/Configuration.h"
namespace fluid_test {
inline station::CellFluidMesh grid(unsigned w,unsigned h,double height=1) {
    station::CellFluidMesh mesh;
    const double volume=12*height,t=293.15;
    const double mass=101325*volume*station::StationMolarMassNitrogenKgPerMol/(station::StationUniversalGasConstantJPerMolK*t);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x)
        mesh.cells.push_back({volume,0,y*w+x,0,station::makeCellFluidState({0,mass,0,0},t)});
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<=w;++x) {
        if (x==0) mesh.faces.push_back({y*w,SIZE_MAX,0,-1,6,0});
        else if (x==w) mesh.faces.push_back({y*w+x-1,SIZE_MAX,0,1,6,0});
        else mesh.faces.push_back({y*w+x-1,y*w+x,0,1,6,1});
    }
    for (unsigned y=0;y<=h;++y) for (unsigned x=0;x<w;++x) {
        if (y==0) mesh.faces.push_back({x,SIZE_MAX,1,-1,6,0});
        else if (y==h) mesh.faces.push_back({(y-1)*w+x,SIZE_MAX,1,1,6,0});
        else mesh.faces.push_back({(y-1)*w+x,y*w+x,1,1,6,1});
    }
    return mesh;
}
inline station::CellFluidMesh earthLikeGrid(unsigned w,unsigned h) {
    auto mesh=grid(w,h);const double t=293.15,pv=935.6,dry=101325-pv;
    const std::array<double,4> pressures{0.2095*dry,(1-0.2095-0.0004)*dry,0.0004*dry,pv};
    const std::array<double,4> molarMass{station::StationMolarMassOxygenKgPerMol,station::StationMolarMassNitrogenKgPerMol,
        station::StationMolarMassCarbonDioxideKgPerMol,station::StationMolarMassWaterKgPerMol};
    for (auto& c:mesh.cells) {
        std::array<double,4> mass{};
        for (int g=0;g<4;++g) mass[g]=pressures[g]*c.volumeM3*molarMass[g]/(station::StationUniversalGasConstantJPerMolK*t);
        c.initial=station::makeCellFluidState(mass,t);
    }
    return mesh;
}
}
