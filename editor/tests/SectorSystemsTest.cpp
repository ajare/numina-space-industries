#include "SectorSystemsGraph.h"
#include "SectorSystemsView.h"
#include "SimulationPlayback.h"
#include "station/editor/EditorSession.h"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace station::editor;
using Installation = nsi::core::Installation;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const SectorSystemNode& atmosphere(const SectorSystemsGraph& graph) {
    auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& n) { return n.type == "Atmosphere"; });
    require(it != graph.nodes.end(), "Missing Atmosphere node");
    return *it;
}
std::string field(const SectorSystemNode& node, const std::string& key) {
    auto it = std::find_if(node.configuration.begin(), node.configuration.end(), [&](const auto& f) { return f.first == key; });
    require(it != node.configuration.end(), "Missing configuration field");
    return it->second;
}
Installation assigned() {
    Installation i(2, 1);
    i.setHullType(std::nullopt); // This fixture isolates the explicit reservoir coupling.
    i.toggleWall(0, i.level(0).verticalWall(1, 0));
    auto yaml = YAML::Load(i.serialize());
    auto sector = yaml["levels"][0]["sectors"][0];
    sector["name"] = "Habitat";
    auto loads = sector["atmosphereLoads"];
    loads["occupants"] = 3;
    loads["oxygenUsePerOccupantKgPerS"] = 0.001;
    loads["co2ProductionPerOccupantKgPerS"] = 0.002;
    loads["sensibleHeatPerOccupantKW"] = 0.03;
    loads["moistureAdditionPerOccupantKgPerS"] = 0.000004;
    loads["leakRateKgPerS"] = 0.005;
    loads["reservoirId"] = "cooling-reservoir";
    loads["exchangerConductanceKWPerK"] = 0.06;
    auto fire = loads["fire"];
    fire["oxygenKgPerS"] = 0.007;
    fire["carbonMonoxideKgPerS"] = 0.0008;
    fire["carbonDioxideKgPerS"] = 0.009;
    fire["heatKW"] = 0.1;
    fire["oxygenThresholdKg"] = 0.11;
    i.deserialize(YAML::Dump(yaml));
    return i;
}
void projection() {
    const auto i = assigned();
    const auto before = i.serialize();
    const auto graph = buildSectorSystemsGraph(i, 0, 0);
    require(graph.nodes.size() == 2 && graph.links.size() == 1, "Did not project atmosphere and authored coupling");
    const auto local=buildSectorSystemsGraph(i,0,0,nullptr,true);
    require(local.nodes.size()==graph.nodes.size() && local.links.size()==graph.links.size(),
        "LocalStructure removed authored gas/reservoir configuration");
    Installation hull(1,1);
    const auto active=buildSectorSystemsGraph(hull,0,0,nullptr,true);
    require(std::none_of(active.nodes.begin(),active.nodes.end(),[](const auto& node){
        return node.type=="Hull" || node.type=="Thermal link";
    }),"LocalStructure configuration invented inactive legacy heat nodes");
    require(!active.nodes.empty(),"LocalStructure configuration hid gas nodes");
    require(graph.title.find("Habitat") != std::string::npos, "Missing selected Sector name");
    const auto& a = atmosphere(graph);
    require(a.id == "sector-0-" + std::to_string(i.level(0).sectors()[0].uid), "Node lacks stable Sector identity");
    require(field(a, "Preset") == "Earth-like" && field(a, "Volume") == "12 m3", "Wrong derived preset/volume");
    require(field(a, "Occupants") == "3" && field(a, "Leak rate") == "0.005 kg/s", "Lost authored loads");
    require(field(a, "Reservoir") == "cooling-reservoir"
        && field(a, "Exchanger conductance") == "0.06 kW/K", "Lost authored heat coupling");
    require(field(a, "Fire fault opt-in").find("not ignited") != std::string::npos
        && field(a, "Fire oxygen consumption") == "0.007 kg/s", "Misrepresented fire configuration");
    const auto& link = graph.links[0];
    require(link.from.nodeType == "Atmosphere" && link.from.portName == "Heat out"
        && link.to.nodeType == "Reservoir" && link.to.portName == "Heat in", "Did not use canonical ports");
    auto missing = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& n) { return n.type == "Reservoir"; });
    require(missing != graph.nodes.end() && missing->unresolved && missing->id == "cooling-reservoir",
            "Invented a reservoir definition instead of displaying unresolved reference");
    for (const auto& n : graph.nodes) for (const auto& p : n.ports) {
        auto descriptors = station::stationPortDescriptors();
        require(std::any_of(descriptors.begin(), descriptors.end(), [&](const auto& d) {
            return d.nodeType == p.reference.nodeType && d.name == p.reference.portName
                && d.direction == p.direction && d.kind == p.kind;
        }), "Invented non-canonical port");
    }
    const auto neighbour = buildSectorSystemsGraph(i, 0, 1);
    require(neighbour.nodes.size() == 1 && neighbour.links.empty(), "Leaked another Sector's components or invented heat path");
    require(atmosphere(neighbour).id != a.id && neighbour.identity != graph.identity, "Selections share identities");
    require(i.serialize() == before, "Projection mutated the Installation");
    require(buildSectorSystemsGraph(i, 10, 0).nodes.empty()
        && buildSectorSystemsGraph(i, 0, 100).nodes.empty(), "Stale selection was not handled");
    Installation voidGeometry({Installation::Level(1, 1, Installation::SectorType::Void)});
    const auto empty = buildSectorSystemsGraph(voidGeometry, 0, 0);
    require(empty.nodes.empty() && empty.message.find("Void Sector") != std::string::npos, "Void geometry invented atmosphere");
    auto yaml = YAML::Load(Installation(1, 1).serialize());
    yaml["levels"][0]["sectors"][0]["atmosphere"] = "Void";
    Installation voidAir; voidAir.deserialize(YAML::Dump(yaml));
    require(buildSectorSystemsGraph(voidAir, 0, 0).nodes.empty(), "Void atmosphere preset invented a compartment");

    Installation resized(2, 1);
    auto full = buildSectorSystemsGraph(resized, 0, 0);
    resized.toggleCell(0, 0, 0);
    const auto selected = resized.level(0).cellData(1, 0).sector;
    auto small = buildSectorSystemsGraph(resized, 0, selected);
    require(atmosphere(full).id == atmosphere(small).id, "Geometry resize renumbered surviving component");
    require(field(atmosphere(full), "Volume") == "24 m3" && field(atmosphere(small), "Volume") == "12 m3",
            "Graph retained stale volume after resize");
}
std::string liveField(const SectorSystemNode& node, const std::string& key) {
    auto it = std::find_if(node.simulation.begin(), node.simulation.end(), [&](const auto& f) { return f.first == key; });
    require(it != node.simulation.end(), "Missing simulation field");
    return it->second;
}
void connectedGraph() {
    Installation i(7,5); const auto g=i.level(0);
    for (unsigned y=0;y<5;++y) {
        i.toggleWall(0,g.verticalWall(2,y)); i.toggleWall(0,g.verticalWall(4,y));
    }
    i.setDoor(0,g.verticalWall(2,2),true); i.setDoor(0,g.verticalWall(4,2),true);
    // Exterior Door on the left Sector's vertical boundary.
    i.setDoor(0,g.verticalWall(0,2),true);
    const auto left=i.level(0).cellData(0,0).sector;
    const auto middle=i.level(0).cellData(2,0).sector;
    const auto rightId="sector-0-"+std::to_string(i.level(0).sectorAt(6,0).uid);
    const auto graph=buildSectorSystemsGraph(i,0,left);
    require(std::count_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.type=="Atmosphere pathway";})==2,"Incident Door pathways absent or recursive");
    require(std::none_of(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.id==rightId;}),"Graph recursively expanded through neighbour");
    require(std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.type=="Hull" && !n.unresolved;}),"Connected Hull is missing or unresolved");
    require(std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.type=="Exterior";}),"Exterior endpoint absent");
    auto hasPin=[&](const station::StationPinRef& pin) {
        for (const auto& n:graph.nodes) for (const auto& p:n.ports) if (p.reference==pin) return true;
        return false;
    };
    for (const auto& link:graph.links) {
        require(hasPin(link.from) && hasPin(link.to),"Graph connection has no rendered endpoint port");
        require(link.from.portName!="Gas out","Door incorrectly wired to Gas out instead of Hosted pathways");
    }
    require(std::count_if(graph.links.begin(),graph.links.end(),[](const auto& l){return l.to.nodeType=="Atmosphere pathway";})==4,"Door endpoint links incomplete");
    require(std::any_of(graph.links.begin(),graph.links.end(),[](const auto& l){return l.from.portName=="Thermal links";}),"Thermal links not projected");
    SimulationPlayback p; p.reset(i); p.togglePlaying();
    p.toggleDoor(0,g.verticalWall(2,2)); p.toggleDoor(0,g.verticalWall(0,2)); p.update(0.1);
    const auto snapshot=p.runtime()->snapshot();
    const auto live=buildSectorSystemsGraph(i,0,left,&snapshot);
    const auto neighbour=buildSectorSystemsGraph(i,0,middle,&snapshot);
    for (const auto& node:live.nodes) {
        if (node.type=="Atmosphere pathway") {
            require(liveField(node,"State")=="Open","Door live open state missing");
            require(!liveField(node,"Flow regime").empty(),"Flow regime absent");
            require(std::isfinite(std::stod(liveField(node,"Bulk gas flow (outward)"))),"Bulk flow absent");
            if (field(node,"End A")==station::StationExteriorAtmosphereId || field(node,"End B")==station::StationExteriorAtmosphereId) {
                require(liveField(node,"Flow regime")=="Choked","Exterior flow not shown choked");
                require(std::stod(liveField(node,"Downstream / upstream pressure"))==0,"Exterior pressure ratio incorrect");
            }
            require(!node.simulationStatus.empty(),"Pathway timestamp absent");
            const auto other=std::find_if(neighbour.nodes.begin(),neighbour.nodes.end(),[&](const auto& n){return n.type==node.type && n.id==node.id;});
            if (other!=neighbour.nodes.end())
                require(std::abs(std::stod(liveField(node,"Nitrogen flow (outward)"))+std::stod(liveField(*other,"Nitrogen flow (outward)")))<1e-8,"Gas flow sign did not follow selected Sector");
        }
        if (node.type=="Thermal link") require(!liveField(node,"Heat flow (outward)").empty(),"Thermal live heat flow absent");
    }
    // Every projected boundary and canonical connection must actually draw.
    ImGui::CreateContext();
    {
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1400,1000}; io.DeltaTime=1.0f/60; io.Fonts->Build();
        SectorSystemsView view;
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({1300,900}); ImGui::Begin("Connected graph");
            view.draw(live); ImGui::End(); ImGui::Render();
        }
        require(view.renderedLinkCount()==live.links.size(),"Connected graph silently omitted links");
    }
    ImGui::DestroyContext();
}
void simulationState() {
    Installation i(2, 1);
    i.toggleWall(0, i.level(0).verticalWall(1, 0));
    auto yaml = YAML::Load(i.serialize());
    auto loads = yaml["levels"][0]["sectors"][0]["atmosphereLoads"];
    loads["occupants"] = 1;
    loads["oxygenUsePerOccupantKgPerS"] = 0.001;
    loads["co2ProductionPerOccupantKgPerS"] = 0.002;
    loads["sensibleHeatPerOccupantKW"] = 0.03;
    loads["moistureAdditionPerOccupantKgPerS"] = 0.000004;
    loads["leakRateKgPerS"] = 0.00001;
    i.deserialize(YAML::Dump(yaml));
    EditorSession session;
    session.replaceDocument(i);
    SimulationPlayback playback;
    const auto absent = buildSectorSystemsGraph(i, 0, 0);
    require(atmosphere(absent).simulation.empty() && !atmosphere(absent).simulationStatus.empty(),
            "Missing runtime invented simulation values");
    require(!playback.snapshotFor(i), "Missing runtime returned telemetry");
    playback.synchronize(i);
    const auto initialSnapshot = playback.snapshotFor(i);
    require(initialSnapshot.has_value(), "Initial matching preview not available");
    const auto initial = buildSectorSystemsGraph(i, 0, 0, &*initialSnapshot);
    require(liveField(atmosphere(initial), "Temperature") == "20 C", "Initial temperature missing");
    require(liveField(atmosphere(initial), "Relative humidity") == "40 %", "Humidity not displayed as percent");
    playback.togglePlaying();
    for (int frame = 0; frame < 10; ++frame) playback.update(0.1);
    const auto live = playback.snapshotFor(i);
    const auto updated = buildSectorSystemsGraph(i, 0, 0, &*live);
    const auto& node = atmosphere(updated);
    require(liveField(node, "Temperature") != liveField(atmosphere(initial), "Temperature")
        && liveField(node, "Oxygen") != liveField(atmosphere(initial), "Oxygen")
        && liveField(node, "Carbon dioxide") != liveField(atmosphere(initial), "Carbon dioxide"),
        "Simulation changes did not reach node telemetry");
    require(node.configuration == atmosphere(initial).configuration, "Live readout overwrote starting configuration");
    require(node.simulationStatus.find("1 s") != std::string::npos, "Live readout omitted committed time");
    require(std::stod(liveField(node, "Leaked gas (total)")) > 0, "Cumulative loss not displayed");
    double fractions = 0;
    for (const auto* label : {"Oxygen", "Nitrogen", "Carbon dioxide", "Water vapour"}) {
        const auto value = liveField(node, label);
        fractions += std::stod(value.substr(value.find(" | ") + 3));
    }
    require(std::abs(fractions - 100) < 0.0001, "Displayed wet mole fractions do not sum to 100 percent");
    // Matching is by stable id, not vector position.
    auto reordered = *live;
    std::reverse(reordered.atmospheres.begin(), reordered.atmospheres.end());
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &reordered)).simulation == node.simulation,
            "Reordered snapshot selected another Sector's atmosphere");
    std::erase_if(reordered.atmospheres, [&](const auto& a) { return a.id == node.id; });
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &reordered)).simulation.empty(),
            "Missing atmosphere displayed another component's telemetry");
    auto failed = *live; failed.healthy = false;
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &failed)).simulationStatus.find("failed") != std::string::npos,
            "Failed run advertised current healthy telemetry");
    auto unavailable = *live; unavailable.enabled = false;
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &unavailable)).simulation.empty(),
            "Disabled simulation invented live values");
    auto nonFinite = *live;
    auto& invalid = *std::find_if(nonFinite.atmospheres.begin(), nonFinite.atmospheres.end(),
                                 [&](const auto& a) { return a.id == node.id; });
    invalid.pressureKPa = 0;
    invalid.temperatureC = std::numeric_limits<double>::quiet_NaN();
    const auto invalidGraph = buildSectorSystemsGraph(i, 0, 0, &nonFinite);
    require(liveField(atmosphere(invalidGraph), "Temperature") == "-- C"
        && liveField(atmosphere(invalidGraph), "Oxygen").find("-- %") != std::string::npos,
        "Unavailable temperature/fraction was shown as a fabricated numeric value");
    playback.togglePlaying();
    playback.update(100);
    const auto paused = playback.snapshotFor(i);
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &*paused)).simulation == node.simulation,
            "Paused view lost or changed its last committed values");
    require(playback.runtime()->snapshot() == *live, "Observation advanced the simulation");
    require(session.installation() == i && !session.unsavedChanges() && !session.canUndo(),
            "Telemetry viewing mutated authoring state");
    playback.reset(i);
    const auto reset = playback.snapshotFor(i);
    require(atmosphere(buildSectorSystemsGraph(i, 0, 0, &*reset)).simulation == atmosphere(initial).simulation,
            "Reset did not restore starting telemetry");
    // The surviving atmosphere id is unchanged by a merge: source matching,
    // not id matching alone, must suppress old values in the editing frame.
    session.toggleWall(0, session.installation().level(0).verticalWall(1, 0));
    require(!playback.snapshotFor(session.installation()), "Geometry edit retained stale live telemetry");
    playback.synchronize(session.installation());
    const auto rebuilt = playback.snapshotFor(session.installation());
    require(rebuilt && rebuilt->elapsedSeconds == 0, "Edited geometry did not reset live readout");
}
void breachGraph() {
    Installation scene(1,1);scene.toggleWall(0,scene.level(0).horizontalWall(0,0));
    SimulationPlayback playback;playback.reset(scene);playback.togglePlaying();playback.update(0.1);
    const auto snapshot=playback.snapshotFor(scene);const auto graph=buildSectorSystemsGraph(scene,0,0,&*snapshot);
    const auto node=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.type=="Atmosphere pathway";});
    require(node!=graph.nodes.end() && node->id.starts_with("breach-"),"Missing exterior Wall absent from configuration graph");
    require(std::any_of(node->simulation.begin(),node->simulation.end(),[](const auto& field){
        return field.first=="Regime convention" && field.second.find("estimate")!=std::string::npos;
    }),"Spatial boundary readout implied an analytic nozzle solve");
    require(snapshot->atmospherePathways.size()==1 && snapshot->atmospherePathways[0].bulkFlowRateKgPerS>0,
        "Breach graph lost outward flow telemetry");
}
void zoomCanvas() {
    Installation i(7,5); const auto g=i.level(0);
    for (unsigned y=0;y<5;++y) {
        i.toggleWall(0,g.verticalWall(2,y)); i.toggleWall(0,g.verticalWall(4,y));
    }
    i.setDoor(0,g.verticalWall(2,2),true); i.setDoor(0,g.verticalWall(4,2),true);
    const auto graph=buildSectorSystemsGraph(i,0,i.level(0).cellData(2,0).sector);
    const auto before=i.serialize();
    ImGui::CreateContext();
    {
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1200,1000}; io.DeltaTime=1.0f/60; io.Fonts->Build();
        SectorSystemsView view;
        auto frame=[&](ImVec2 mouse,float wheel=0,bool left=false) {
            io.MousePos=mouse; io.MouseWheel=wheel; io.MouseDown[0]=left;
            ImGui::NewFrame(); ImGui::SetNextWindowPos({30,30}); ImGui::SetNextWindowSize({760,700});
            ImGui::Begin("Zoom canvas");
            const auto padding=ImGui::GetStyle().FramePadding;
            const auto fontSize=ImGui::GetFontSize();
            view.draw(graph);
            require(ImGui::GetStyle().FramePadding.x==padding.x && ImGui::GetStyle().FramePadding.y==padding.y
                && ImGui::GetFontSize()==fontSize,"Canvas zoom leaked font/style scale into parent panel");
            ImGui::End(); ImGui::Render();
            require(!view.linkCreationStarted() && view.renderedLinkCount()==graph.links.size(),"Zoom changed read-only links");
        };
        frame({-100,-100}); frame({-100,-100}); frame({-100,-100});
        require(view.zoom()<1,"Large graph did not initially zoom out");
        auto fitted=[&] {
            const auto origin=view.canvasOrigin(),size=view.canvasSize();
            for (const auto& n:graph.nodes) {
                const auto pos=view.nodePosition(n.type,n.id),extent=view.nodeDimensions(n.type,n.id);
                require(pos && extent,"Zoomed graph lost node geometry");
                require(pos->x>=origin.x-1 && pos->y>=origin.y-1
                    && pos->x+extent->x<=origin.x+size.x+1 && pos->y+extent->y<=origin.y+size.y+1,
                    "Initial fit clipped a node");
            }
        };
        fitted();
        const auto origin=view.canvasOrigin(),size=view.canvasSize();
        const ImVec2 anchor(origin.x+size.x*0.5f,origin.y+size.y*0.5f);
        frame(anchor);
        const auto& n=graph.nodes.front(); const auto start=*view.nodePosition(n.type,n.id);
        const float initial=view.zoom();
        frame(anchor,1);
        require(view.zoom()>initial,"Mouse wheel did not zoom in over canvas");
        const auto moved=*view.nodePosition(n.type,n.id); const float ratio=view.zoom()/initial;
        require(std::abs(moved.x-(anchor.x+(start.x-anchor.x)*ratio))<0.1f
            && std::abs(moved.y-(anchor.y+(start.y-anchor.y)*ratio))<0.1f,"Wheel zoom drifted away from pointer anchor");
        frame(anchor,-1);
        require(std::abs(view.zoom()-initial)<1e-5f,"Wheel down did not zoom out");
        const auto retained=view.zoom();
        frame({-100,-100}); frame({-100,-100},3);
        require(view.zoom()==retained,"Wheel outside canvas changed zoom");
        // Node dragging must remain in screen-space pixels at non-unit zoom.
        const auto pos=*view.nodePosition(n.type,n.id);
        const ImVec2 header(pos.x+20*view.zoom(),pos.y+10*view.zoom());
        frame(header); frame(header,0,true);
        frame({header.x+35,header.y+25},0,true); frame({header.x+35,header.y+25});
        const auto dragged=*view.nodePosition(n.type,n.id);
        require(std::abs(dragged.x-pos.x-35)<0.1f && std::abs(dragged.y-pos.y-25)<0.1f,"Zoom broke node drag geometry");
        frame({-100,-100});
        const auto saved=*view.nodePosition(n.type,n.id);
        require(std::abs(saved.x-dragged.x)<0.1f && std::abs(saved.y-dragged.y)<0.1f,"Zoomed node placement was not retained");
        view.fitToView(); frame({-100,-100}); frame({-100,-100}); fitted();
        require(i.serialize()==before,"Zoom modified Installation");
    }
    ImGui::DestroyContext();
}
void readOnlyCanvas() {
    ImGui::CreateContext();
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = ImVec2(1400, 1000); io.DeltaTime = 1.0f / 60;
        io.Fonts->Build();
        EditorSession session;
        session.replaceDocument(assigned());
        const auto before = session.installation().serialize();
        const auto graph = buildSectorSystemsGraph(session.installation(), 0, 0);
        const auto& a = atmosphere(graph);
        SectorSystemsView view;
        // Isolate the actual collapsible-header frame vertices, not just its
        // text/item bounds: an oversized frame can escape an otherwise small node.
        constexpr ImU32 configurationColor = IM_COL32(17, 231, 83, 255);
        for (auto color : {ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive})
            ImGui::GetStyle().Colors[color] = ImGui::ColorConvertU32ToFloat4(configurationColor);
        auto frame = [&](ImVec2 mouse, bool left = false) {
            io.MousePos = mouse; io.MouseDown[0] = left;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1300, 950));
            ImGui::Begin("Sector", nullptr, ImGuiWindowFlags_NoSavedSettings);
            view.draw(graph);
            ImGui::End();
            ImGui::Render();
            require(!view.linkCreationStarted(), "Read-only port started a link drag");
        };
        frame(ImVec2(-100, -100)); frame(ImVec2(-100, -100));
        require(view.renderedLinkCount() == 1, "Authored coupling was not rendered");
        auto requireHeaderWidth = [&] {
            float left = std::numeric_limits<float>::max(), right = std::numeric_limits<float>::lowest();
            float top = left;
            auto* drawData = ImGui::GetDrawData();
            for (int list = 0; list < drawData->CmdListsCount; ++list)
                for (const auto& vertex : drawData->CmdLists[list]->VtxBuffer)
                    if (vertex.col == configurationColor) {
                        left = std::min(left, vertex.pos.x); right = std::max(right, vertex.pos.x);
                        top = std::min(top, vertex.pos.y);
                    }
            require(right >= left, "Configuration header frame was not rendered");
            require(right - left <= 440.01f, "Configuration header frame extends beyond node content width");
            // Multiple bounded sections can share this colour. The first
            // header is Configuration; target it rather than the union's centre.
            return ImVec2((left + right) * 0.5f, top + ImGui::GetFrameHeight() * 0.5f);
        };
        const auto configurationHeader = requireHeaderWidth();
        const auto collapsedGas = view.portPosition({a.type, a.id, "Gas out"});
        frame(configurationHeader); frame(configurationHeader, true); frame(configurationHeader);
        requireHeaderWidth();
        const auto expandedGas = view.portPosition({a.type, a.id, "Gas out"});
        require(collapsedGas && expandedGas && expandedGas->y > collapsedGas->y + 100,
                "Configuration header did not expand for width regression check");
        const auto node = view.nodePosition(a.type, a.id);
        const auto pin = view.portPosition(graph.links[0].from);
        const auto target = view.portPosition(graph.links[0].to);
        require(node && pin && target, "Nodes or ports missing from real canvas");
        // Click and drag a port across to another port: not even an unfinished
        // link preview may be created on the read-only canvas.
        frame(*pin); frame(*pin, true);
        frame(ImVec2(pin->x + 20, pin->y + 20), true);
        frame(*target, true); frame(*target);
        require(view.renderedLinkCount() == 1, "Pin gesture changed rendered links");
        const auto freePin = view.portPosition({a.type, a.id, "Gas out"});
        require(freePin.has_value(), "Missing unused port");
        frame(*freePin); frame(*freePin, true);
        frame(ImVec2(freePin->x + 50, freePin->y + 30), true);
        frame(ImVec2(freePin->x + 50, freePin->y + 30));
        require(view.renderedLinkCount() == 1, "Unused port created a link");
        // Moving nodes is transient presentation state, not configuration editing.
        const ImVec2 header(node->x + 50, node->y + 10);
        frame(header); frame(header, true);
        frame(ImVec2(header.x + 100, header.y + 80), true);
        frame(ImVec2(header.x + 100, header.y + 80));
        auto after = view.nodePosition(a.type, a.id);
        require(after && std::abs(after->x - node->x - 100) < 0.01f
            && std::abs(after->y - node->y - 80) < 0.01f, "Node drag did not move the view layout");
        frame(ImVec2(-100, -100));
        const auto retained = view.nodePosition(a.type, a.id);
        require(retained && std::abs(retained->x - after->x) < 0.01f && std::abs(retained->y - after->y) < 0.01f,
                "Next frame overwrote user node placement");
        require(view.renderedLinkCount() == 1, "Node placement changed authored connections");
        requireHeaderWidth();
        require(session.installation().serialize() == before && !session.unsavedChanges()
            && !session.canUndo(), "Read-only view changed document or undo history");
        // Switching to an empty Sector must not retain ports or links.
        const auto empty = buildSectorSystemsGraph(session.installation(), 4, 0);
        ImGui::NewFrame(); ImGui::Begin("Sector"); view.draw(empty); ImGui::End(); ImGui::Render();
        require(view.renderedLinkCount() == 0 && !view.portPosition(graph.links[0].from), "Stale graph survived selection removal");
    }
    ImGui::DestroyContext();
}
}
int main() {
    try { projection(); simulationState(); connectedGraph(); breachGraph(); zoomCanvas(); readOnlyCanvas(); std::cout << "Sector systems graph passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
