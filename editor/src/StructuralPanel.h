#pragma once
#include "SimulationPlayback.h"
namespace station::editor {
struct StructuralPanelState {
    bool sector=false;
    int page=0, surface=0;
    std::optional<nsi::systems::StructuralOwner> owner;
};
void drawStructuralPanel(const SimulationPlayback&,nsi::systems::StructuralOwner,StructuralPanelState&);
}
