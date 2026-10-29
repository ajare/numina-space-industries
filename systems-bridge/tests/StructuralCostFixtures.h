#pragma once
// Cost-study fixtures only, not a production geometry expansion API.
#include <nsi/core/Installation.h>
#include <sstream>
#include <string>
namespace structural_cost {
using I=nsi::core::Installation;
inline I fixture(const std::string& name, unsigned size=64) {
    const bool many=name=="many-sectors", tall=name=="tall-offset";
    const unsigned width=tall ? size/2 : size, depth=size;
    std::ostringstream y;
    y<<"version: 4\nname: Cost study\nhull: MultiLayerBlanket\npartition: InsulatedPartition\nlevels:\n";
    for (unsigned l=0;l<(tall ? 3u : 1u);++l) {
        const bool empty=tall && l==1;
        const unsigned nx=many ? size/4 : 1, ny=many ? size/8 : 1;
        y<<"- width: "<<width<<"\n  depth: "<<depth<<"\n  offset_x: "<<(l==2 ? size/4 : 0)
         <<"\n  offset_y: "<<(l==2 ? size/8 : 0)<<"\n  sectors:\n";
        for (unsigned s=0;s<nx*ny;++s) {
            y<<"  - {name: S"<<s<<", type: "<<(empty ? "Void" : "Interior")<<", height: "<<(tall && l==0 ? 2 : 1)
             <<", uid: "<<s+1<<", atmosphere: "<<(empty ? "Void" : "Earth-like");
            if (name=="evacuated") y<<", atmosphereLoads: {leakRateKgPerS: 1000000000}";
            if (name=="loaded") y<<", atmosphereLoads: {occupants: "<<width*depth
                <<", oxygenUsePerOccupantKgPerS: 0.00001, co2ProductionPerOccupantKgPerS: 0.000007, sensibleHeatPerOccupantKW: 0.1, moistureAdditionPerOccupantKgPerS: 0.000001}";
            y<<"}\n";
        }
        y<<"  cells:\n";
        for (unsigned j=0;j<depth;++j) for (unsigned x=0;x<width;++x)
            y<<"  - {sector: "<<(many ? (j/8)*nx+x/4 : 0)<<", floor: {}, wiring: {}, piping: {}}\n";
        y<<"  walls: [";
        bool first=true;
        auto wall=[&](bool edge,bool partition,bool breach) {
            if (!first) y<<", ";first=false;
            y<<(empty || breach ? "None" : edge ? "Exterior" : partition ? "Interior" : "None");
        };
        for (unsigned j=0;j<=depth;++j) for (unsigned x=0;x<width;++x)
            wall(j==0 || j==depth,many && j%8==0,(name=="breaches" || name=="sustained-breaches") && (j==0 || j==depth) && (x==0 || x==width-1));
        for (unsigned j=0;j<depth;++j) for (unsigned x=0;x<=width;++x)
            wall(x==0 || x==width,many && x%4==0,false);
        y<<"]\n";
    }
    I result;result.deserialize(y.str());return result;
}
}
