#include "station/editor/EditorSession.h"

#include <stdexcept>
#include <utility>
namespace station::editor {

EditorSession::EditorSession()
    : mStation(Installation::createDefault()), mBaseline(mStation) {}

FileResult EditorSession::request(DocumentAction action, UnsavedChangesPrompt& prompt,
                                  FileDialogs& dialogs, DocumentFiles& files,
                                  std::uint32_t width, std::uint32_t depth) {
    // A window close can interrupt a held pointer gesture. Keep that whole
    // edit undoable if the user cancels, and save exactly the visible geometry.
    endStroke();
    try {
        if (unsavedChanges()) {
            switch (prompt.confirm(action, mPath)) {
                case UnsavedDecision::Cancel:
                    return {FileOutcome::Cancelled, {}};
                case UnsavedDecision::Save: {
                    auto result = save(dialogs, files);
                    if (result.outcome != FileOutcome::Success) return result;
                    break;
                }
                case UnsavedDecision::Discard:
                    break;
            }
        }
        switch (action) {
            case DocumentAction::New:
                newDocument(width, depth);
                return {FileOutcome::Success, {}};
            case DocumentAction::Open:
                return open(dialogs, files);
            case DocumentAction::Exit:
                return {FileOutcome::Success, {}};
        }
        return {FileOutcome::Failed, "Unknown document action"};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Document action failed: ") + error.what()};
    }
}

void EditorSession::newDocument(std::uint32_t width, std::uint32_t depth) {
    replaceDocument(Installation(width, depth));
}

void EditorSession::replaceDocument(Installation station) {
    // A different document starts clean: no stroke in flight, and no
    // history that could undo the previous document's Cells into it.
    mStroke.reset();
    mWallStrokeState.reset();
    mUndoHistory.clear();
    mRedoHistory.clear();
    mStation = std::move(station);
    mBaseline = mStation;
    mHasDocument = true;
    mEditingEnabled = true; // New/Open replace the scenario rather than edit a run.
    mPath.clear();
}

FileResult EditorSession::open(FileDialogs& dialogs, DocumentFiles& files) {
    try {
        const auto selection = dialogs.openYaml();
        if (selection.outcome != FileOutcome::Success) {
            return {selection.outcome, selection.diagnostic};
        }
        if (selection.path.empty()) return {FileOutcome::Failed, "Open returned an empty path"};
        auto loaded = files.read(selection.path);
        replaceDocument(std::move(loaded));
        mPath = selection.path;
        return {FileOutcome::Success, {}};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Open failed: ") + error.what()};
    }
}

FileResult EditorSession::save(FileDialogs& dialogs, DocumentFiles& files) {
    if (mPath.empty()) return saveAs(dialogs, files);
    return saveTo({FileOutcome::Success, mPath, {}}, files);
}

FileResult EditorSession::saveAs(FileDialogs& dialogs, DocumentFiles& files) {
    try {
        return saveTo(dialogs.saveYaml(mPath), files);
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Save failed: ") + error.what()};
    }
}

FileResult EditorSession::saveTo(const FileSelection& selection, DocumentFiles& files) {
    if (selection.outcome != FileOutcome::Success) {
        return {selection.outcome, selection.diagnostic};
    }
    try {
        if (selection.path.empty()) throw std::runtime_error("Save returned an empty path");
        files.write(selection.path, mStation);
        mPath = selection.path;
        mBaseline = mStation;
        mHasDocument = true;
        return {FileOutcome::Success, {}};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Save failed: ") + error.what()};
    }
}

void EditorSession::setEditingEnabled(bool enabled) {
    if (!enabled) endStroke();
    mEditingEnabled = enabled;
}

void EditorSession::commit(Installation before) {
    if (mStation == before) return;
    mUndoHistory.push_back(std::move(before));
    mRedoHistory.clear();
}
void EditorSession::edit(const std::function<void(Installation&)>& operation) {
    if (!mEditingEnabled) return;
    endStroke();
    auto next=mStation;
    operation(next);
    next.validate();
    auto before=std::move(mStation);
    mStation=std::move(next);
    commit(std::move(before));
}
void EditorSession::toggleCell(std::size_t l,std::uint32_t x,std::uint32_t y) {
    beginStroke(l,x,y); paintStroke(x,y); endStroke();
}
void EditorSession::beginStroke(std::size_t l,std::uint32_t x,std::uint32_t y) {
    if (!mEditingEnabled) return;
    endStroke();
    auto type=mStation.level(l).cell(x,y);
    mStrokeState=type==SectorType::Interior ? SectorType::Void : SectorType::Interior;
    mStrokeLevel=l; mStroke=mStation;
}
void EditorSession::paintStroke(std::uint32_t x,std::uint32_t y) {
    if (!mEditingEnabled || !mStroke || mWallStrokeState.has_value() || !mStation.level(mStrokeLevel).contains(x,y)) return;
    try { mStation.setCell(mStrokeLevel,x,y,mStrokeState); }
    catch (...) { cancelStroke(); throw; }
}
void EditorSession::cancelStroke() {
    mStation=std::move(*mStroke);
    mStroke.reset();
    mWallStrokeState.reset();
}
void EditorSession::endStroke() {
    if (!mStroke) return;
    auto before=std::move(*mStroke);
    mStroke.reset();
    mWallStrokeState.reset();
    commit(std::move(before));
}
void EditorSession::toggleWall(std::size_t l,std::size_t w) {
    beginWallStroke(l,w);
    paintWallStroke(w);
    endStroke();
}
void EditorSession::setDoor(std::size_t l,std::size_t w,bool present) {
    edit([&](auto& i){ i.setDoor(l,w,present); });
}
void EditorSession::beginWallStroke(std::size_t l,std::size_t w) {
    if (!mEditingEnabled) return;
    endStroke();
    const auto walls=mStation.level(l).walls();
    if (w>=walls.size()) throw std::out_of_range("Wall outside Level");
    const bool add=walls[w].type==Installation::WallType::None;
    mStroke=mStation;
    mStrokeLevel=l;
    mWallStrokeState=add;
}
void EditorSession::paintWallStroke(std::size_t w) {
    if (!mEditingEnabled || !mStroke || !mWallStrokeState.has_value()) return;
    const auto walls=mStation.level(mStrokeLevel).walls();
    if (w>=walls.size()) return;
    const bool present=walls[w].type!=Installation::WallType::None;
    if (present==*mWallStrokeState) return;
    try { mStation.toggleWall(mStrokeLevel,w); }
    catch (...) { cancelStroke(); throw; }
}
void EditorSession::setSector(std::size_t l,std::size_t s,std::string name,int height) {
    edit([&](auto& i){ i.setSector(l,s,std::move(name),height); });
}
void EditorSession::setOffset(std::size_t l,int x,int y) {
    edit([&](auto& i){ i.setOffset(l,x,y); });
}
void EditorSession::addLevel(std::uint32_t w,std::uint32_t d,int x,int y) {
    edit([&](auto& i){ i.addLevel(w,d,x,y); });
}
bool EditorSession::removeTopLevel() {
    bool removed=false; edit([&](auto& i){ removed=i.removeTopLevel(); }); return removed;
}
void EditorSession::setName(std::string name) {
    edit([&](auto& i){ i.setName(std::move(name)); });
}
void EditorSession::setHullType(std::optional<Installation::HullType> type) {
    edit([&](auto& i){ i.setHullType(type); });
}
void EditorSession::setPartitionType(std::optional<Installation::PartitionType> type) {
    edit([&](auto& i){ i.setPartitionType(type); });
}
void EditorSession::setOrbit(std::optional<Installation::Orbit> orbit) {
    edit([&](auto& i){ i.setOrbit(orbit); });
}
void EditorSession::undo() {
    if (!mEditingEnabled) return;
    endStroke(); if (mUndoHistory.empty()) return;
    mRedoHistory.push_back(std::move(mStation));
    mStation=std::move(mUndoHistory.back()); mUndoHistory.pop_back();
}
void EditorSession::redo() {
    if (!mEditingEnabled) return;
    endStroke(); if (mRedoHistory.empty()) return;
    mUndoHistory.push_back(std::move(mStation));
    mStation=std::move(mRedoHistory.back()); mRedoHistory.pop_back();
}
} // namespace station::editor
