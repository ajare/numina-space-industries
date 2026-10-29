#include "CellPlaybackRuntime.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
using namespace station::editor;
int main(int argc,char** argv) {
    try {
    const double seconds=argc>2 ? std::stod(argv[2]) : 0.1;
    if (argc>3 || !std::isfinite(seconds) || seconds<=0 || (argc>1 && std::string(argv[1])!="rest"
        && std::string(argv[1])!="breaches" && std::string(argv[1])!="sustained-breaches")) {
        std::cerr<<"Usage: editor-cell-playback-benchmark [rest|breaches|sustained-breaches [seconds > 0]]\n";return 2;
    }
    for (const std::string scenario:{"rest","breaches","sustained-breaches"}) {
        if (argc>1 && scenario!=argv[1]) continue;
        double total=0,maximum=0;
        for (int trial=0;trial<12;++trial) {
            nsi::core::Installation scene(64,64);
            if (scenario!="rest") {
                const auto& g=scene.level(0);
                for (const auto wall:{g.horizontalWall(0,0),g.horizontalWall(63,0),g.horizontalWall(0,64),g.horizontalWall(63,64)}) scene.toggleWall(0,wall);
            }
            CellPlaybackRuntime runtime(scene);
            if (scenario=="sustained-breaches" && !runtime.update(1)) {std::cerr<<runtime.diagnostics()[0].message<<'\n';return 1;}
            const auto start=std::chrono::steady_clock::now();
            if (!runtime.update(seconds)) {std::cerr<<runtime.diagnostics()[0].message<<'\n';return 1;}
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (trial>=2) {total+=ms;maximum=std::max(maximum,ms);}
        }
        std::cout<<scenario<<": "<<total/10<<" ms mean, "<<maximum<<" ms max / "<<seconds<<" simulated s (4096 Cells, includes snapshots)\n";
    }
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
}
