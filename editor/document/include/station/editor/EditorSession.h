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
    // The session holds a live Installation from the moment it is built.
    // The grid the editor opens on is a document like any other, not a
    // placeholder that has to be earned before it will run. Whether one
    // has been read from or written to disk is what path() answers -
    // empty until a successful Open or Save - and nothing about being
    // live turns on it. There used to be a hasDocument() here saying
    // otherwise, and the only thing it ever gated was playback, which is
    // why the editor opened with a playable station on screen under a
    // greyed-out Play button and no way of being told why.
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
    // When the mission begins, as seconds since 1970-01-01T00:00:00Z.
    // Always midnight on the picked day; see nsi::core::CivilDate.h.
    void setStartDateUtcS(std::int64_t seconds);
    // The authored sky. Clearing it means no orbit: a dark sky and no
    // stellar gain, never a substituted default.
    void setOrbit(std::optional<Installation::Orbit> orbit);
    void setSolarArrayEnabled(bool enabled);
    void setSolarArray(std::optional<Installation::SolarArray> array);
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
    bool mEditingEnabled = true;
    std::filesystem::path mPath;
    std::size_t mStrokeLevel = 0;
    SectorType mStrokeState = SectorType::Void;
    std::optional<bool> mWallStrokeState;
    std::optional<Installation> mStroke;
    std::vector<Installation> mUndoHistory, mRedoHistory;
};
} // namespace station::editor
