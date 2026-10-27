# Space Station documents

The Editor's File menu provides New, Open, Save and Save As. Untitled Save
requests a destination; named Save reuses its path. Successful Open resets
history; Save keeps history and establishes a clean geometry baseline. Only
successful writes adopt a new path.

The title's `*` and the statusbar's “Unsaved changes” indicate geometry that
is different from the current baseline, including during a paint stroke.
Undo/redo and equivalent geometry on a new history branch can restore clean
status; an untouched default document never needs a prompt.

New, Open, menu Exit and window close all use `EditorSession::request`.
Dirty documents offer Save, Discard and Cancel through an external
`UnsavedChangesPrompt` adapter. Save continues only after a successful write;
cancelling a destination chooser or any prompt/dialog/write failure blocks
the action. Discard permits the action without eagerly resetting state, so a
subsequent cancelled/refused Open still leaves the old document intact. A
successful Save followed by cancelled Open keeps that save's path/baseline.
Dismissing the prompt means Cancel. The GUI handles both exit sources through
the same guard and only exits when the session authorizes it.

Schema version 1 is readable YAML:

```yaml
version: 1
decks:
  - width: 16
    height: 16
    rows:
      - "################"
      # ... exactly 16 quoted rows, top to bottom ...
```

Each row runs left to right: `#` means Filled, `.` means Void. This milestone
requires exactly one 16×16 Deck. All-Void Decks and disconnected floor regions
are valid. Missing/invalid fields, duplicate required-map keys, unsupported
versions/dimensions/Deck counts, malformed rows and extra YAML documents are
refused before replacing the active document. Additional unknown fields are
ignored; they are not preserved by Save.

`EditorSession` owns geometry, path, history and the saved baseline. Its
`FileDialogs` and `DocumentFiles` adapters keep native UI and file I/O outside
the session. `YamlDocumentFiles` uses yaml-cpp for strict whole-document
validation and the existing `Promethium::Serialization` YAML emitter and
transactional file writer for saves. Failures and cancellation are distinct
outcomes; failed loads/writes leave the session intact.

The GUI brackets NFD initialization/shutdown with a scoped adapter. Linux uses
the XDG desktop portal backend (build dependency: dbus-1 development files;
runtime: a desktop portal with a FileChooser implementation). Windows/macOS
use NFD's native backends.

Release CTest consumers exercise real files with substituted dialog outcomes,
without GL or desktop windows. Native smoke check: launch the Editor, Save an
untitled document through the native YAML chooser, New, then Open that saved
file through the native chooser; verify the title adopts its path and geometry
returns. Closing a chooser without selecting a file must leave the document
unchanged. Also check mouse click/drag, Ctrl+Z/Ctrl+Y, Overview updates,
Save/Discard/Cancel before New/Open/Exit, and window-close cancellation. A
cancelled first-save chooser must leave the pending action blocked and the
unsaved indicator visible.
