#pragma once
#include "station/editor/Installation.h"
#include "station/editor/DocumentIO.h"
#include "station/editor/DocumentActions.h"
#include <functional>
#include <optional>
#include <vector>

namespace station::editor {
class EditorSession {
public:
    EditorSession();
    FileResult request(DocumentAction action, UnsavedChangesPrompt& prompt,
                       FileDialogs& dialogs, DocumentFiles& files,
                       std::uint32_t width = 16, std::uint32_t depth = 16);
    void newDocument(std::uint32_t width = 16, std::uint32_t depth = 16);
    void replaceDocument(Installation installation);
    // Startup geometry is a placeholder until New/Open (or a successful Save).
    // This is independent of path(): newly created unsaved documents are active.
    bool hasDocument() const noexcept { return mHasDocument; }
    const Installation& installation() const noexcept { return mStation; }
    const Installation& station() const noexcept { return mStation; }
    const std::filesystem::path& path() const noexcept { return mPath; }
    bool unsavedChanges() const { return mStation != mBaseline; }
    FileResult open(FileDialogs& dialogs, DocumentFiles& files);
    FileResult save(FileDialogs& dialogs, DocumentFiles& files);
    FileResult saveAs(FileDialogs& dialogs, DocumentFiles& files);
    // The host supplies playback policy; locking finishes any in-flight stroke.
    void setEditingEnabled(bool enabled);
    bool editingEnabled() const noexcept { return mEditingEnabled; }
    void toggleCell(std::size_t level, std::uint32_t x, std::uint32_t y);
    void beginStroke(std::size_t level, std::uint32_t x, std::uint32_t y);
    bool strokeActive() const noexcept { return mStroke.has_value(); }
    void paintStroke(std::uint32_t x, std::uint32_t y);
    void endStroke();
    void toggleWall(std::size_t level, std::size_t wall);
    void setDoor(std::size_t level, std::size_t wall, bool present);
    // Starting on None paints present Walls; starting on a Wall erases.
    // Each visited edge chooses Interior/Exterior from its adjacent Cells.
    void beginWallStroke(std::size_t level, std::size_t wall);
    void paintWallStroke(std::size_t wall);
    void setSector(std::size_t level, std::size_t sector, std::string name, int height);
    void setOffset(std::size_t level, int x, int y);
    void addLevel(std::uint32_t width, std::uint32_t depth, int x = 0, int y = 0);
    bool removeTopLevel();
    void setName(std::string name);
    void setHullType(std::optional<Installation::HullType> type);
    void setPartitionType(std::optional<Installation::PartitionType> type);
    // The authored sky. Clearing it means no orbit: a dark sky and no
    // stellar gain, never a substituted default.
    void setOrbit(std::optional<Installation::Orbit> orbit);
    void undo();
    void redo();
    bool canUndo() const noexcept { return mEditingEnabled && !mUndoHistory.empty(); }
    bool canRedo() const noexcept { return mEditingEnabled && !mRedoHistory.empty(); }
private:
    // Full snapshots preserve Sector identities/metadata and automatically
    // changed Walls exactly, rather than attempting to invert flood fills.
    void edit(const std::function<void(Installation&)>& operation);
    void commit(Installation before);
    void cancelStroke();
    FileResult saveTo(const FileSelection& selection, DocumentFiles& files);
    Installation mStation, mBaseline;
    bool mHasDocument = false;
    bool mEditingEnabled = true;
    std::filesystem::path mPath;
    std::size_t mStrokeLevel = 0;
    SectorType mStrokeState = SectorType::Void;
    std::optional<bool> mWallStrokeState;
    std::optional<Installation> mStroke;
    std::vector<Installation> mUndoHistory, mRedoHistory;
};
} // namespace station::editor
