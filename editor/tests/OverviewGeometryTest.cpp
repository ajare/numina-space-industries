#include "OverviewGeometry.h"
#include "station/editor/EditorSession.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.001f; }
}

int main() {
    using namespace station::editor;
    try {
        require(buildOverviewCells(Installation{}, 640, 480, 1).empty(),
                "An unknown Level draws nothing");
        Installation sparse({Level(4, 2, SectorType::Void)});
        require(buildOverviewCells(sparse, 400, 300).empty(),
                "All-Void Level must draw no grid or floor geometry");
        sparse.setCell(0, 1, 0, SectorType::Interior);
        sparse.setCell(0, 3, 1, SectorType::Interior);
        auto cells = buildOverviewCells(sparse, 400, 300);
        require(cells.size() == 2, "Only Interior Cells produce tiles");
        require(close(cells[0].x, 150) && close(cells[0].y, 100) &&
                    close(cells[0].halfSize, 50),
                "First tile is square and centred in the Level footprint");
        require(close(cells[1].x, 350) && close(cells[1].y, 200),
                "Cell coordinates are preserved, with y downwards");
        cells = buildOverviewCells(sparse, 200, 400);
        require(close(cells[0].x, 75) && close(cells[0].y, 175) &&
                    close(cells[0].halfSize, 25),
                "Resize refits and centres the Level without stretching Cells");
        cells = buildOverviewCells(sparse, 1, 1);
        for (const auto& cell : cells) {
            require(cell.x - cell.halfSize >= 0 && cell.x + cell.halfSize <= 1 &&
                        cell.y - cell.halfSize >= 0 && cell.y + cell.halfSize <= 1,
                    "Tiny viewport still fits the entire Level");
        }
        require(buildOverviewCells(sparse, 0, 10).empty() &&
                    buildOverviewCells(sparse, 10, 0).empty(),
                "Zero viewport draws nothing");
        bool refused=false;
        try { Level invalid(0,0); } catch (const std::invalid_argument&) { refused=true; }
        require(refused,"Degenerate Levels are rejected by the core");

        EditorSession session;
        require(buildOverviewCells(session.station(), 640, 480).size() == 256,
                "Default station renders all 256 floor tiles");
        session.toggleCell(0, 0, 0);
        require(buildOverviewCells(session.station(), 640, 480).size() == 255,
                "Editing immediately removes the floor tile");
        session.undo();
        require(buildOverviewCells(session.station(), 640, 480).size() == 256,
                "Undo restores the floor tile");
        session.redo();
        require(buildOverviewCells(session.station(), 640, 480).size() == 255,
                "Redo removes the floor tile again");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
