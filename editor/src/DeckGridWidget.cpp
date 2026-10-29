#include "DeckGridWidget.h"

#include "imgui.h"
#include "station/editor/GridPicking.h"
#include <station/AtmosphereMetrics.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMaxCellPx = 34.0f;
constexpr float kMinCellPx = 8.0f;

ImU32 cellColour(station::editor::SectorType state) {
    // Interior reads as a lit floor plate, Void as open space behind it.
    return state == station::editor::SectorType::Interior
               ? IM_COL32(126, 168, 200, 255)
               : IM_COL32(26, 29, 34, 255);
}

// The Cell under a screen point, or nullopt when the point is outside the
// drawn grid.
std::optional<DeckCellCoord> cellAt(const ImVec2& origin, float cell,
                                   const station::editor::Level& level,
                                   const ImVec2& point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < origin.x || point.y < origin.y ||
        point.x >= origin.x + cell * static_cast<float>(level.width()) ||
        point.y >= origin.y + cell * static_cast<float>(level.depth())) {
        return std::nullopt;
    }
    const std::uint32_t x =
        static_cast<std::uint32_t>((point.x - origin.x) / cell);
    const std::uint32_t y =
        static_cast<std::uint32_t>((point.y - origin.y) / cell);
    if (!level.contains(x, y)) {
        return std::nullopt;
    }
    return DeckCellCoord{x, y};
}

}  // namespace

DeckPointerEvent drawDeckGrid(const char* id,
                             const station::editor::Level& level, EditMode mode,
                             std::optional<int> selectedSector,
                             std::span<const station::editor::SectorColour> thermalColours,
                             bool doorControlsEnabled, std::span<const std::size_t> openDoors,
                             std::span<const station::editor::SectorColour> cellColours,
                             std::span<const station::StationCellAtmosphereSnapshot> atmosphere,
                             std::size_t levelIndex,bool forceArrows,double forceHalfLengthN) {
    DeckPointerEvent event;

    const float available_width = ImGui::GetContentRegionAvail().x;
    const float columns =
        static_cast<float>(std::max<std::uint32_t>(1, level.width()));
    const float cell = std::clamp(std::floor(available_width / columns),
                                 kMinCellPx, kMaxCellPx);
    const ImVec2 grid_size(cell * static_cast<float>(level.width()),
                          cell * static_cast<float>(level.depth()));

    ImGui::InvisibleButton(id, grid_size, ImGuiButtonFlags_MouseButtonLeft |
        (mode==EditMode::Door ? ImGuiButtonFlags_MouseButtonRight : 0));

    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool mapped=!thermalColours.empty() || !cellColours.empty();
    auto validCell=[&](const station::StationCellAtmosphereSnapshot& c) {
        if (c.level!=levelIndex || c.gridIndex>=level.cellCount()) return false;
        const auto& s=level.sectors()[level.cells()[c.gridIndex].sector];
        return s.uid==c.sectorUid && s.type==station::editor::SectorType::Interior
            && s.atmosphere!=nsi::core::Installation::AtmospherePreset::Void;
    };

    for (std::uint32_t y = 0; y < level.depth(); ++y) {
        for (std::uint32_t x = 0; x < level.width(); ++x) {
            const ImVec2 a(origin.x + cell * static_cast<float>(x),
                          origin.y + cell * static_cast<float>(y));
            const ImVec2 b(a.x + cell, a.y + cell);
            const auto sector = level.cellData(x,y).sector;
            ImU32 colour = cellColour(level.cell(x,y));
            if (sector >= 0 && std::size_t(sector) < thermalColours.size()) {
                const auto& thermal = thermalColours[sector];
                colour = ImGui::ColorConvertFloat4ToU32({thermal.red,thermal.green,thermal.blue,1});
            }
            const auto index=std::size_t(y)*level.width()+x;
            if (index<cellColours.size()) {
                const auto& c=cellColours[index];colour=ImGui::ColorConvertFloat4ToU32({c.red,c.green,c.blue,1});
            }
            draw->AddRectFilled(a, b, colour);
            if (selectedSector && sector == *selectedSector) {
                if (!mapped) draw->AddRectFilled(a,b,IM_COL32(245,190,60,85));
                else draw->AddRect({a.x+2,a.y+2},{b.x-2,b.y-2},IM_COL32(245,190,60,255),0,0,2);
            }
        }
    }

    if (forceArrows) for (const auto& c:atmosphere) {
        if (!validCell(c) || !c.resolvedGas || !c.forcesEnabled || !c.forceAvailable) continue;
        const double force=std::hypot(c.forceN[0],c.forceN[1]);
        const float length=cell*0.72f*static_cast<float>(station::editor::airflowLoadArrowFraction(force,forceHalfLengthN));
        if (length<0.5f) continue;
        const float dx=static_cast<float>(c.forceN[0]/force),dy=static_cast<float>(c.forceN[1]/force);
        const ImVec2 center{origin.x+cell*(float(c.gridIndex%level.width())+0.5f),origin.y+cell*(float(c.gridIndex/level.width())+0.5f)};
        const ImVec2 tail{center.x-dx*length/2,center.y-dy*length/2},tip{center.x+dx*length/2,center.y+dy*length/2};
        const float head=std::min(4.0f,length*0.35f);const auto colour=IM_COL32(255,245,160,240);
        draw->AddLine(tail,tip,colour,1.5f);
        draw->AddLine(tip,{tip.x-head*dx+head*dy*0.6f,tip.y-head*dy-head*dx*0.6f},colour,1.5f);
        draw->AddLine(tip,{tip.x-head*dx-head*dy*0.6f,tip.y-head*dy+head*dx*0.6f},colour,1.5f);
    }
    const ImU32 grid_colour = IM_COL32(70, 78, 90, 255);
    for (std::uint32_t i = 0; i <= level.width(); ++i) {
        const float x = origin.x + cell * static_cast<float>(i);
        draw->AddLine(ImVec2(x, origin.y),
                     ImVec2(x, origin.y + grid_size.y), grid_colour);
    }
    for (std::uint32_t i = 0; i <= level.depth(); ++i) {
        const float y = origin.y + cell * static_cast<float>(i);
        draw->AddLine(ImVec2(origin.x, y),
                     ImVec2(origin.x + grid_size.x, y), grid_colour);
    }
    draw->AddRect(origin, ImVec2(origin.x + grid_size.x, origin.y + grid_size.y),
                 ImGui::GetColorU32(ImGuiCol_Border));

    const ImGuiIO& io = ImGui::GetIO();
    const auto wall = station::editor::nearestWall(level,
        (io.MousePos.x-origin.x)/cell, (io.MousePos.y-origin.y)/cell);
    const double mouseX=(io.MousePos.x-origin.x)/cell, mouseY=(io.MousePos.y-origin.y)/cell;
    const auto horizontalCount=std::size_t(level.width())*(std::size_t(level.depth())+1);
    const bool horizontalHit=wall && *wall<horizontalCount;
    const double edgeFraction=horizontalHit ? mouseX-std::floor(mouseX) : mouseY-std::floor(mouseY);
    const double doorWidthFraction=nsi::core::DoorWidthM/(horizontalHit ? nsi::core::CellWidthM : nsi::core::CellDepthM);
    const bool hoveredDoor=ImGui::IsItemHovered() && wall
        && level.walls()[*wall].type==nsi::core::Installation::WallType::Door
        && std::abs(edgeFraction-0.5)<=doorWidthFraction/2
        && std::min(std::abs(mouseX-std::round(mouseX)),std::abs(mouseY-std::round(mouseY)))*cell<=6.0;
    auto isOpen=[&](std::size_t w) { return std::find(openDoors.begin(),openDoors.end(),w)!=openDoors.end(); };
    auto drawWall = [&](std::size_t index, ImU32 colour, float thickness, bool open=false, bool highlight=false) {
        const auto horizontal = std::size_t(level.width())*(std::size_t(level.depth())+1);
        ImVec2 a,b;
        if (index < horizontal) {
            a=ImVec2(origin.x+float(index%level.width())*cell,
                     origin.y+float(index/level.width())*cell);
            b=ImVec2(a.x+cell,a.y);
        } else {
            const auto i=index-horizontal;
            a=ImVec2(origin.x+float(i%(std::size_t(level.width())+1))*cell,
                     origin.y+float(i/(std::size_t(level.width())+1))*cell);
            b=ImVec2(a.x,a.y+cell);
        }
        if (level.walls()[index].type==nsi::core::Installation::WallType::Door) {
            const float width=static_cast<float>(nsi::core::DoorWidthM/
                (index<horizontal ? nsi::core::CellWidthM : nsi::core::CellDepthM));
            const float inset=(1-width)/2;
            const ImVec2 d(b.x-a.x,b.y-a.y);
            const ImVec2 left(a.x+d.x*inset,a.y+d.y*inset);
            const ImVec2 right(b.x-d.x*inset,b.y-d.y*inset);
            const auto wallColour=highlight ? colour : IM_COL32(215,225,235,255);
            draw->AddLine(a,left,wallColour,highlight ? thickness : 3.0f);
            draw->AddLine(right,b,wallColour,highlight ? thickness : 3.0f);
            if (!open) draw->AddLine(left,right,colour,thickness);
        } else draw->AddLine(a,b,colour,thickness);
    };
    using WallType=nsi::core::Installation::WallType;
    for (std::size_t i=0;i<level.walls().size();++i) {
        auto type=level.walls()[i].type;
        if (type!=WallType::None)
            drawWall(i,type==WallType::Door ? IM_COL32(70,210,225,255) :
                type==WallType::Exterior ? IM_COL32(245,180,95,255) : IM_COL32(215,225,235,255),
                type==WallType::Door ? 6.0f : 3.0f,type==WallType::Door && isOpen(i));
    }
    if ((mode==EditMode::Wall || mode==EditMode::Door) && ImGui::IsItemHovered() && wall
        && (!doorControlsEnabled || hoveredDoor)) {
        const bool valid=doorControlsEnabled || mode!=EditMode::Door || level.canPlaceDoor(*wall);
        drawWall(*wall,valid ? IM_COL32(100,255,140,255) : IM_COL32(255,100,100,255),5.0f,
            level.walls()[*wall].type==WallType::Door && isOpen(*wall),true);
    }
    const std::optional<DeckCellCoord> under =
        cellAt(origin, cell, level, io.MousePos);

    if (mode!=EditMode::Wall && mode!=EditMode::Door && ImGui::IsItemHovered() && under.has_value()) {
        const ImVec2 a(origin.x + cell * static_cast<float>(under->x),
                      origin.y + cell * static_cast<float>(under->y));
        if (!mapped)
            draw->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255,255,255,46));
        else draw->AddRect({a.x+1,a.y+1},{a.x+cell-1,a.y+cell-1},IM_COL32(255,255,255,255));
    }

    // The press is reported on activation, the drag while this widget still
    // holds the mouse, and the release globally so a stroke always ends even
    // if the button came up after the pointer left the grid.
    if (ImGui::IsItemActivated()) {
        event.phase = DeckPointerPhase::Press;
        event.cell = under;
        event.erase = mode==EditMode::Door && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (mode==EditMode::Door && !event.erase && io.KeyShift) {
            // Shift-click never falls back to authoring, even before Play or
            // when the nearest edge is not actually under the pointer.
            event.phase=DeckPointerPhase::None;
            if (doorControlsEnabled && hoveredDoor && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
                event.phase=DeckPointerPhase::Press;
                event.toggleDoor=true;
            }
        }
    } else if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        event.phase = DeckPointerPhase::Drag;
        event.cell = under;
    } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        event.phase = DeckPointerPhase::Release;
        event.cell = under;
    }

    // A hold must begin in Injector mode on this grid. Sampling is per frame,
    // never a persistent source left enabled after release/mode/focus changes.
    ImGui::PushID(id);const auto injectorGesture=ImGui::GetID("InjectorGesture");ImGui::PopID();
    auto* storage=ImGui::GetStateStorage();
    if (ImGui::IsItemActivated()) storage->SetBool(injectorGesture,mode==EditMode::Injector && ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    if (mode!=EditMode::Injector || !ImGui::IsItemActive() || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || io.AppFocusLost)
        storage->SetBool(injectorGesture,false);
    event.injectHeld=storage->GetBool(injectorGesture) && ImGui::IsItemHovered() && under.has_value();

    if (mode==EditMode::Door && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        event.phase=DeckPointerPhase::Release;
    event.wall=wall;
    const std::optional<DeckCellCoord> readout =
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) ? under : std::nullopt;
    if (readout.has_value()) {
        const station::editor::SectorType state =
            level.cell(readout->x, readout->y);
        ImGui::Text("Cell (%u, %u): %s", readout->x, readout->y,
                   state == station::editor::SectorType::Interior ? "Interior"
                                                             : "Void");
        const auto index=std::size_t(readout->y)*level.width()+readout->x;
        const auto it=std::find_if(atmosphere.begin(),atmosphere.end(),[&](const auto& c){return c.gridIndex==index && validCell(c);});
        if (it!=atmosphere.end()) {
            if (!it->resolvedGas) ImGui::TextUnformatted("Pressure below resolved range (~0 kPa); temperature, composition, wind and force unavailable.");
            else {
                ImGui::Text("Pressure %.6g kPa | Temperature %.3f C | RH %.2f%%",it->pressureKPa,it->temperatureC,100*it->relativeHumidity);
                if (!it->windEnabled) ImGui::TextUnformatted("Wind diagnostics: disabled");
                else if (!it->windAvailable) ImGui::TextUnformatted("Wind diagnostics: unavailable");
                else ImGui::Text("Wind (%.3f, %.3f) m/s | Speed %.3f m/s",it->velocityX,it->velocityY,it->windSpeedMS);
                if (!it->forcesEnabled) ImGui::TextUnformatted("Object forces: disabled");
                else if (!it->forceAvailable) ImGui::TextUnformatted("Object forces: unavailable");
                else {
                    ImGui::Text("Reference load (%.6g, %.6g) N | Magnitude %.6g N",it->forceN[0],it->forceN[1],std::hypot(it->forceN[0],it->forceN[1]));
                    ImGui::Text("Drag (%.6g, %.6g) N | Pressure (%.6g, %.6g) N",it->dragForceN[0],it->dragForceN[1],it->pressureForceN[0],it->pressureForceN[1]);
                    ImGui::Text("Density %.6g kg/m3 | Dynamic pressure %.6g Pa",it->densityKgPerM3,it->dynamicPressurePa);
                    ImGui::TextUnformatted("Reference: stationary, A=1 m2, V=1 m3, Cd=1; one-way load estimate.");
                }
                const double molar[]={station::StationMolarMassOxygenKgPerMol,station::StationMolarMassNitrogenKgPerMol,
                    station::StationMolarMassCarbonDioxideKgPerMol,station::StationMolarMassWaterKgPerMol};
                double total=0;for (int g=0;g<4;++g) total+=it->massKg[g]/molar[g];
                if (total>0) ImGui::Text("Wet mole %%: O2 %.3f | N2 %.3f | CO2 %.3f | H2O %.3f",
                    100*it->massKg[0]/molar[0]/total,100*it->massKg[1]/molar[1]/total,
                    100*it->massKg[2]/molar[2]/total,100*it->massKg[3]/molar[3]/total);
            }
        } else if (state==station::editor::SectorType::Interior) {
            ImGui::TextUnformatted(level.sectorAt(readout->x,readout->y).atmosphere==nsi::core::Installation::AtmospherePreset::Void
                ? "No Cell atmosphere (Void preset)." : "Live Cell atmosphere unavailable.");
        }
    } else {
        ImGui::Text("%zu Interior Cells of %zu",
                   level.interiorCount(), level.cellCount());
    }

    if (mode==EditMode::Door && hoveredDoor && doorControlsEnabled)
        ImGui::SetTooltip("%s — Shift+left-click to %s",isOpen(*wall) ? "Open" : "Closed",isOpen(*wall) ? "close" : "open");
    return event;
}
