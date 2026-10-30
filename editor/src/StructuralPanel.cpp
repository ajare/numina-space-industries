#include "StructuralPanel.h"
#include "StellarExposureTrace.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <string>

namespace station::editor {
namespace {
std::string label(nsi::systems::StructuralSurfaceId id) {
    const auto& a=id.assembly;
    return "("+std::to_string(a.origin.x)+","+std::to_string(a.origin.y)+","+std::to_string(a.origin.z)
        +") axis "+std::to_string(a.axis)+" part "+std::to_string(static_cast<int>(a.part))+" side "+std::to_string(id.side);
}
void diagram(const StructuralGraph& graph,std::size_t surface) {
    const auto& s=graph.surfaces[surface];
    const std::size_t endpoints=graph.contactCount.written+1+(s.gasCell ? 1 : 0);
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const float width=std::max(360.f,ImGui::GetContentRegionAvail().x);
    const float height=std::max(100.f,32.f*endpoints);
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 source{origin.x+65,origin.y+height/2};
    auto node=[&](ImVec2 centre,const char* text) {
        draw->AddRectFilled({centre.x-60,centre.y-12},{centre.x+60,centre.y+12},IM_COL32(55,85,100,255),3);
        draw->AddText({centre.x-55,centre.y-7},IM_COL32_WHITE,text);
    };
    std::size_t row=0;
    auto endpoint=[&](const char* text) {
        const ImVec2 target{origin.x+width-65,origin.y+16+32.f*row++};
        draw->AddLine(source,target,IM_COL32(180,180,180,255),1.5f);node(target,text);
    };
    if (s.gasCell) endpoint(s.gasTemperatureK ? "Gas / film" : "Gas unresolved");
    endpoint(s.cavity ? "Cavity field" : "Exterior face");
    for (std::size_t i=0;i<graph.contactCount.written;++i) {
        const auto text="Solid edge "+std::to_string(i+1);endpoint(text.c_str());
    }
    node(source,"Selected side");ImGui::Dummy({width,height});
    ImGui::TextWrapped("One-hop topology only (lines are connections, not flow arrows). Edge numbers resolve to stable endpoint IDs below; cavity portals are listed separately. Gas films detach when unresolved.");
}
void group(const char* name,const nsi::systems::StructuralSurfaceSummary& s) {
    if (auto k=s.areaWeightedTemperatureK()) {
        ImGui::Text("%s: %.3f C | min %.3f / max %.3f C",name,*k-273.15,s.minTemperatureK-273.15,s.maxTemperatureK-273.15);
        ImGui::Text("  %.3f m2 | capacity %.6g J/K | stored %.6g J",s.areaM2,s.capacityJPerK,static_cast<double>(s.storedEnergyJ));
    } else ImGui::Text("%s: unavailable (no surfaces)",name);
}
}
void drawStructuralPanel(const SimulationPlayback& playback,nsi::systems::StructuralOwner owner,StructuralPanelState& view) {
    if (!ImGui::CollapsingHeader("Structure / real thermal topology",ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto* runtime=playback.runtime();
    if (!runtime || !runtime->structuralSnapshot().enabled) {
        ImGui::TextWrapped("Structural observations unavailable. Gas Thermal is a separate view.");return;
    }
    if (view.owner!=owner) {view.owner=owner;view.page=0;view.surface=0;}
    const auto cavity=structuralCavity(*runtime,owner);
    if (ImGui::Checkbox("Inspect whole Sector (exposed sides)",&view.sector)) {view.page=0;view.surface=0;}
    const auto summary=view.sector && cavity ? runtime->structuralSector(*cavity) : runtime->structuralCell(owner);
    ImGui::TextWrapped("Area-weighted exposed-side temperatures, not air or an equilibrated room temperature. Capacities/energies count selected sides only; shared assembly mass is split, not duplicated.");
    group("All surfaces",summary.all);group("Floor",summary.floor);group("Ceiling",summary.ceiling);group("Sides",summary.sides);
    const auto& state=runtime->structuralSnapshot();
    ImGui::Text("Committed simulation time: %.3f s",state.observedSeconds);
    ImGui::Text("Model: %s | preset: %s",std::string(state.modelRevision).c_str(),std::string(state.presetRevision).c_str());
    ImGui::Text("Global unique solid energy: %.6g J",static_cast<double>(state.solidEnergyJ));
    ImGui::Text("Global gas internal / kinetic: %.6g / %.6g J",static_cast<double>(state.gasInternalEnergyJ),static_cast<double>(state.gasKineticEnergyJ));
    if (ImGui::TreeNode("Global heat-path rates / cumulative ledgers")) {
        ImGui::TextUnformatted("Last accepted whole-call mean W / cumulative J (not selected-Sector rates)");
        const auto& r=state.rates;const auto& p=state.paths;
        auto path=[](const char* name,long double w,long double j){ImGui::Text("%s: %+.6g W / %+.6g J",name,static_cast<double>(w),static_cast<double>(j));};
        path("Gas into solids (+)",r.gasIntoSolidW,p.gasIntoSolidJ);
        path("Solid conduction magnitude",r.solidConductionMagnitudeW,p.solidConductionMagnitudeJ);
        path("Interior radiation magnitude",r.interiorRadiationMagnitudeW,p.interiorRadiationMagnitudeJ);
        path("Exterior portal escape (+ outward)",r.portalEscapeW,p.portalEscapeJ);
        path("Exterior solid radiation (+ outward)",r.exteriorRadiationW,p.exteriorRadiationJ);
        path("Absorbed solar (+ inward)",r.absorbedSolarW,p.absorbedSolarJ);
        ImGui::TextWrapped("Internal magnitudes are not net losses or additional sources. Interior radiation counts both surfaces of an internal exchange.");
        ImGui::Text("Global energy residual %.6g J | cavity closure %.6g W",static_cast<double>(state.residualJ),state.work.cavityClosureW);
        ImGui::TreePop();
    }
    if (cavity) {
        auto history=playback.structuralHistory(*cavity);
        if (!history.empty() && ImPlot::BeginPlot("Sector surface history###StructuralHistory",{-1,180})) {
            ImPlot::SetupAxes("Simulation time (s)","Surface temperature (C)",0,ImPlotAxisFlags_AutoFit);
            const double now=history.back().seconds;
            ImPlot::SetupAxisLimits(ImAxis_X1,std::max(0.0,now-60),std::max(60.0,now),ImGuiCond_Always);
            // The sky rides its own axis. Exposure is a 0..1 fraction, so on
            // the temperature scale it would either flatten to a straight
            // line or flatten the hull curves with it.
            ImPlot::SetupAxis(ImAxis_Y2,"Stellar exposure",ImPlotAxisFlags_AuxDefault);
            ImPlot::SetupAxisLimits(ImAxis_Y2,0.0,1.0,ImGuiCond_Always);
            struct Series {std::span<const StructuralSample> samples;double StructuralSample::*field;};
            auto plot=[&](const char* name,double StructuralSample::*field) {
                Series series{history,field};
                ImPlot::PlotLineG(name,[](int i,void* data){const auto& s=*static_cast<Series*>(data);const auto& p=s.samples[i];return ImPlotPoint(p.seconds,p.*s.field);},&series,static_cast<int>(history.size()));
            };
            plot("Area mean",&StructuralSample::meanC);plot("Floor",&StructuralSample::floorC);
            plot("Ceiling",&StructuralSample::ceilingC);plot("Sides",&StructuralSample::sidesC);
            plot("Minimum",&StructuralSample::minimumC);plot("Maximum",&StructuralSample::maximumC);
            drawStellarExposureTrace(playback.exposureHistory(),
                exposureTraceLabel(state.modelRevision,state.presetRevision));
            ImPlot::EndPlot();
        }
        ImGui::TextWrapped("History is the selected Sector's exposed surfaces, independent of Cell selection; unavailable measurements are gaps. Pausing/refusal adds no samples; Reset clears history.");
        ImGui::TextWrapped("Shaded bands are Eclipse and the stepped line is Stellar exposure on the right axis, both read from the evaluator the hull is actually under; a gap means no orbital sky, not a zero.");
    }
    ImGui::TextWrapped("Radiation: visibility-free diffuse well-mixed Sector cavity approximation. Door leaves use binary poses: open surfaces park at a deterministic Interior endpoint; no hinge simulation. Apertures use clear area, not gas Cd.");
    if (ImGui::InputInt("Surface page (16 per page)",&view.page)) {view.page=std::max(0,view.page);view.surface=0;}
    auto graph=structuralGraph(*runtime,owner,cavity,view.sector,static_cast<std::size_t>(view.page),static_cast<std::size_t>(view.surface));
    ImGui::Text("Surfaces: %zu shown / %zu total | page %d",graph.surfaceCount.written,graph.surfaceCount.total,view.page);
    if (graph.surfaceCount.written==0) {ImGui::TextUnformatted("No surfaces on this page; choose page 0.");return;}
    view.surface=std::clamp(view.surface,0,static_cast<int>(graph.surfaceCount.written)-1);
    for (std::size_t i=0;i<graph.surfaceCount.written;++i) {
        const auto text=label(graph.surfaces[i].id);
        ImGui::RadioButton(text.c_str(),&view.surface,static_cast<int>(i));
    }
    graph=structuralGraph(*runtime,owner,cavity,view.sector,view.page,view.surface);
    diagram(graph,view.surface);
    const auto& selected=graph.surfaces[view.surface];
    ImGui::Text("Selected surface %.3f C | %.3f m2 | %.6g J/K",selected.temperatureK-273.15,selected.areaM2,selected.capacityJPerK);
    ImGui::Text("Stored energy %.6g J | gas-only capacity %.6g J/K",static_cast<double>(selected.storedEnergyJ),selected.gasCapacityJPerK);
    if (selected.gasTemperatureK) ImGui::Text("Gas endpoint %.3f C -> surface: film %.6g W/K",*selected.gasTemperatureK-273.15,selected.gasContactWPerK);
    else ImGui::TextUnformatted("Gas endpoint temperature unavailable (missing / unresolved); not cold air.");
    if (selected.cavity) ImGui::Text("Surface <-> cavity %zu (algebraic field, no stored energy)",*selected.cavity);
    else ImGui::TextUnformatted("Surface faces exterior (no cavity owner)");
    ImGui::Text("Immediate solid edges: %zu shown / %zu total (no recursive expansion)",graph.contactCount.written,graph.contactCount.total);
    for (std::size_t i=0;i<graph.contactCount.written;++i) {
        const auto& edge=graph.contacts[i];const auto target=edge.a==selected.id ? edge.b : edge.a;
        const auto endpoint=runtime->model().structuralSurface(target);
        ImGui::TextWrapped("Solid edge %zu: selected <-> %s | %.6g W/K",i+1,label(target).c_str(),edge.conductanceWPerK);
        if (endpoint) ImGui::Text("  Endpoint %.3f C",endpoint->temperatureK-273.15);
    }
    ImGui::Text("Cavity portal edges: %zu shown / %zu total (first 16)",graph.portalCount.written,graph.portalCount.total);
    for (std::size_t i=0;i<graph.portalCount.written;++i) {
        const auto& p=graph.portals[i];
        const auto endpoint=p.b ? "cavity "+std::to_string(*p.b) : "exterior";
        ImGui::Text("Cavity %zu <-> %s: %.6g m2",p.a,endpoint.c_str(),p.areaM2);
        if (p.outwardPowerW) ImGui::Text("  Instantaneous A -> B: %+.6g W",static_cast<double>(*p.outwardPowerW));
        else ImGui::TextUnformatted("  Irradiance / power unavailable (including after Door controls until commit)");
    }
}
}
