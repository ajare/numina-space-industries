// Real YAML and files through the shared session seam, without NFD or GL.
#include "station/editor/EditorSession.h"
#include "serialization/TransactionalFileWriter.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace station::editor;
namespace fs = std::filesystem;

int checks = 0;
void require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

struct TempFiles {
    fs::path dir = fs::temp_directory_path() /
        ("nsi-persistence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempFiles() { fs::create_directories(dir); }
    ~TempFiles() { std::error_code error; fs::remove_all(dir, error); }
};

struct Dialogs : FileDialogs {
    FileSelection next{FileOutcome::Cancelled, {}, {}};
    int opens = 0;
    int saves = 0;
    fs::path suggested;
    FileSelection openYaml() override { ++opens; return next; }
    FileSelection saveYaml(const fs::path& current) override {
        ++saves;
        suggested = current;
        return next;
    }
    void choose(const fs::path& path) { next = {FileOutcome::Success, path, {}}; }
};

std::string readText(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
void writeText(const fs::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
    require(output.good(), "write test fixture");
}
std::string yaml(const std::string& version = "1", const std::string& width = "16",
                 const std::string& height = "16", int count = 16,
                 const std::string& row = "################") {
    std::string text = "version: " + version + "\ndecks:\n  - width: " + width +
        "\n    height: " + height + "\n    rows:\n";
    for (int i = 0; i < count; ++i) text += "      - \"" + row + "\"\n";
    return text;
}

void roundTrips(TempFiles& temp) {
    YamlDocumentFiles files;
    Dialogs dialogs;
    EditorSession session;
    require(session.path().empty() && !session.unsavedChanges(), "initial untitled clean state");
    // Default, asymmetric mixed rows (including a leading #), empty and
    // disconnected geometry must all round-trip exactly, not transposed.
    for (int pattern = 0; pattern < 4; ++pattern) {
        session.newDocument();
        if (pattern == 1) {
            session.toggleCell(0, 2, 9);
            session.toggleCell(0, 15, 3);
            session.toggleCell(0, 0, 7);
        } else if (pattern >= 2) {
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x) session.toggleCell(0, x, y);
            if (pattern == 3) {
                session.toggleCell(0, 1, 2);
                session.toggleCell(0, 14, 13);
            }
        }
        const auto expected = session.station();
        const auto path = temp.dir / ("pattern-" + std::to_string(pattern) + ".yaml");
        dialogs.choose(path);
        const int requests = dialogs.saves;
        require(session.save(dialogs, files).outcome == FileOutcome::Success, "first Save succeeds");
        require(dialogs.saves == requests + 1, "untitled Save requests destination");
        require(session.path() == path && !session.unsavedChanges(), "successful Save adopts clean path");
        const auto emitted = readText(path);
        require(emitted.find("version: 1") != std::string::npos, "readable versioned YAML");
        std::size_t quotedRows = 0;
        for (std::size_t pos = 0; (pos = emitted.find("- \"", pos)) != std::string::npos; pos += 3) ++quotedRows;
        require(quotedRows == 16, "every row is explicitly quoted, including all-Void rows");
        session.toggleCell(0, 5, 8);
        session.undo();
        require(!session.unsavedChanges(), "undo restores saved baseline");
        session.redo();
        require(session.unsavedChanges(), "redo leaves saved baseline");
        dialogs.choose(path);
        require(session.open(dialogs, files).outcome == FileOutcome::Success, "Open saved YAML");
        require(session.station() == expected, "every Cell round-trips exactly");
        require(session.path() == path && !session.unsavedChanges(), "Open establishes clean named document");
        require(!session.canUndo() && !session.canRedo(), "successful Open clears both history branches");
        session.undo();
        session.redo();
        require(session.station() == expected, "history cannot restore prior document");
    }
}

void destinationsAndFailures(TempFiles& temp) {
    YamlDocumentFiles files;
    Dialogs dialogs;
    EditorSession session;
    auto original = temp.dir / "original.yaml";
    auto copy = temp.dir / "copy.yaml";
    dialogs.choose(original);
    session.toggleCell(0, 1, 4);
    require(session.save(dialogs, files).outcome == FileOutcome::Success, "initial Save");
    session.toggleCell(0, 2, 5);
    const auto changed = session.station();
    const int requests = dialogs.saves;
    dialogs.next = {FileOutcome::Failed, {}, "should not show this dialog"};
    require(session.save(dialogs, files).outcome == FileOutcome::Success, "named Save reuses path");
    require(dialogs.saves == requests, "named Save requires no dialog");
    require(!session.unsavedChanges(), "named Save updates baseline");
    session.undo();
    require(session.unsavedChanges(), "undo past new saved baseline is dirty");
    session.redo();
    require(!session.unsavedChanges() && session.station() == changed, "redo reaches new saved baseline");
    dialogs.choose(copy);
    require(session.saveAs(dialogs, files).outcome == FileOutcome::Success, "Save As succeeds");
    require(session.path() == copy && dialogs.suggested == original, "Save As adopts destination, suggests current name");
    session.toggleCell(0, 9, 10);
    session.undo(); // keep both history branches observable
    const auto before = session.station();
    const bool dirty = session.unsavedChanges();
    for (const auto outcome : {FileOutcome::Cancelled, FileOutcome::Failed}) {
        dialogs.next = {outcome, {}, outcome == FileOutcome::Failed ? "native dialog error" : ""};
        for (int operation = 0; operation < 2; ++operation) {
            const auto result = operation == 0 ? session.open(dialogs, files) : session.saveAs(dialogs, files);
            require(result.outcome == outcome, "cancellation distinguished from dialog failure");
            require((outcome == FileOutcome::Cancelled) == result.diagnostic.empty(), "only dialog failures report diagnostics");
            require(session.station() == before && session.path() == copy && session.unsavedChanges() == dirty,
                    "cancelled/failed dialog preserves geometry, path and baseline");
            require(session.canUndo() && session.canRedo(), "cancelled/failed dialog preserves both history branches");
        }
    }
    session.redo();
    const auto unsaved = session.station();
    require(session.unsavedChanges(), "setup unsaved edit");
    // A nonexistent parent is a deterministic write failure, including as root.
    dialogs.choose(temp.dir / "missing-directory" / "bad.yaml");
    auto result = session.saveAs(dialogs, files);
    require(result.outcome == FileOutcome::Failed && !result.diagnostic.empty(), "write failure reported");
    require(session.path() == copy && session.station() == unsaved && session.unsavedChanges(), "failed Save As adopts nothing");
    const auto bytes = readText(copy);
    serialization::setTransactionalWriteFailureAfterBytesForTesting(8);
    result = session.save(dialogs, files);
    serialization::setTransactionalWriteFailureAfterBytesForTesting(0);
    require(result.outcome == FileOutcome::Failed && !result.diagnostic.empty(), "partial write failure reported");
    require(readText(copy) == bytes, "failed Save does not damage existing file");
    require(session.path() == copy && session.station() == unsaved && session.unsavedChanges(), "failed Save preserves editable unsaved state");
    dialogs.choose(temp.dir / "missing.yaml");
    result = session.open(dialogs, files);
    require(result.outcome == FileOutcome::Failed && !result.diagnostic.empty(), "read failure reported");
    require(session.path() == copy && session.station() == unsaved && session.unsavedChanges(), "read failure preserves document");
    session.newDocument();
    require(session.path().empty() && !session.unsavedChanges() && !session.canUndo() && !session.canRedo(),
            "New resets path, baseline and history");
    for (const auto outcome : {FileOutcome::Cancelled, FileOutcome::Failed}) {
        dialogs.next = {outcome, {}, outcome == FileOutcome::Failed ? "dialog failed" : ""};
        require(session.save(dialogs, files).outcome == outcome, "untitled Save propagates dialog outcome");
        require(session.path().empty() && !session.unsavedChanges(), "unsuccessful first Save stays untitled");
    }
}

void invalidDocuments(TempFiles& temp) {
    YamlDocumentFiles files;
    Dialogs dialogs;
    EditorSession session;
    dialogs.choose(temp.dir / "baseline.yaml");
    require(session.save(dialogs, files).outcome == FileOutcome::Success, "baseline saved");
    session.toggleCell(0, 4, 11);
    session.toggleCell(0, 7, 12);
    session.undo();
    const auto before = session.station();
    const auto path = session.path();
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"malformed YAML", "version: [\n"},
        {"empty document", ""},
        {"missing version", "decks: []\n"},
        {"unsupported version", yaml("2")},
        {"invalid version type", yaml("true")},
        {"missing Decks", "version: 1\n"},
        {"zero Decks", "version: 1\ndecks: []\n"},
        {"multiple Decks", yaml() + "  - width: 16\n    height: 16\n    rows: []\n"},
        {"Decks map instead of sequence", "version: 1\ndecks: {}\n"},
        {"Deck not map", "version: 1\ndecks: [16]\n"},
        {"wrong width", yaml("1", "15")},
        {"wrong height", yaml("1", "16", "17")},
        {"non-integer dimension", yaml("1", "16.5")},
        {"missing dimensions", "version: 1\ndecks: [{rows: []}]\n"},
        {"missing rows", "version: 1\ndecks: [{width: 16, height: 16}]\n"},
        {"too few rows", yaml("1", "16", "16", 15)},
        {"too many rows", yaml("1", "16", "16", 17)},
        {"short row", yaml("1", "16", "16", 16, "###############")},
        {"long row", yaml("1", "16", "16", 16, "#################")},
        {"invalid symbol", yaml("1", "16", "16", 16, "########x#######")},
        {"duplicate version", yaml() + "version: 2\n"},
        {"extra YAML document", yaml() + "---\ninvalid: true\n"},
        {"rows map", "version: 1\ndecks: [{width: 16, height: 16, rows: {a: b}}]\n"},
        {"row map", "version: 1\ndecks: [{width: 16, height: 16, rows: [ {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {} ]}]\n"}
    };
    const auto bad = temp.dir / "bad.yaml";
    for (const auto& [name, text] : invalid) {
        writeText(bad, text);
        dialogs.choose(bad);
        auto result = session.open(dialogs, files);
        require(result.outcome == FileOutcome::Failed && !result.diagnostic.empty(), name + " refused with diagnostic");
        require(session.station() == before && session.path() == path && session.unsavedChanges(), name + " preserves document");
        require(session.canUndo() && session.canRedo(), name + " preserves history availability");
        session.redo();
        require(session.station().deck(0).cell(7, 12) == CellState::Void, name + " preserves redo content");
        session.undo();
        require(session.station() == before, name + " preserves undo content");
    }
}
struct Prompt : UnsavedChangesPrompt {
    UnsavedDecision decision = UnsavedDecision::Cancel;
    int requests = 0;
    DocumentAction requestedAction = DocumentAction::New;
    fs::path requestedPath;
    bool fail = false;
    UnsavedDecision confirm(DocumentAction action, const fs::path& path) override {
        ++requests;
        requestedAction = action;
        requestedPath = path;
        if (fail) throw std::runtime_error("prompt unavailable");
        return decision;
    }
};

struct GuardDialogs : FileDialogs {
    FileSelection destination{FileOutcome::Cancelled, {}, {}};
    FileSelection source{FileOutcome::Cancelled, {}, {}};
    int opens = 0;
    int saves = 0;
    FileSelection openYaml() override { ++opens; return source; }
    FileSelection saveYaml(const fs::path&) override { ++saves; return destination; }
};

constexpr DocumentAction actions[] = {DocumentAction::New, DocumentAction::Open, DocumentAction::Exit};

void guardedActionMatrix(TempFiles& temp) {
    YamlDocumentFiles files;
    const auto savedPath = temp.dir / "guard-saved.yaml";
    const auto openedPath = temp.dir / "guard-opened.yaml";
    EditorSession fixture;
    fixture.toggleCell(0, 13, 2);
    files.write(openedPath, fixture.station());
    const auto loaded = fixture.station();
    for (bool named : {false, true}) {
        for (auto action : actions) {
            for (auto decision : {UnsavedDecision::Save, UnsavedDecision::Discard, UnsavedDecision::Cancel}) {
                EditorSession session;
                GuardDialogs dialogs;
                dialogs.destination = {FileOutcome::Success, savedPath, {}};
                dialogs.source = {FileOutcome::Success, openedPath, {}};
                Prompt prompt;
                prompt.decision = decision;
                if (named) require(session.save(dialogs, files).outcome == FileOutcome::Success, "named matrix setup");
                dialogs.saves = 0;
                session.toggleCell(0, 3, 7);
                session.toggleCell(0, 8, 11);
                session.undo(); // both branches must survive cancellation/Exit
                const auto before = session.station();
                const auto beforePath = session.path();
                require(session.unsavedChanges(), "matrix starts dirty");
                const auto result = session.request(action, prompt, dialogs, files);
                require(prompt.requests == 1 && prompt.requestedAction == action && prompt.requestedPath == beforePath,
                        "dirty action prompts with current document identity");
                const bool cancelled = decision == UnsavedDecision::Cancel;
                const bool saved = decision == UnsavedDecision::Save;
                require(result.outcome == (cancelled ? FileOutcome::Cancelled : FileOutcome::Success),
                        "Save/Discard continue, Cancel aborts action");
                require(dialogs.saves == (saved && !named ? 1 : 0), "first-save destination requested only when necessary");
                require(dialogs.opens == (action == DocumentAction::Open && !cancelled ? 1 : 0),
                        "Open selection is not requested before consent/save succeeds");
                if (saved) {
                    EditorSession disk;
                    Dialogs chooser;
                    chooser.choose(savedPath);
                    require(disk.open(chooser, files).outcome == FileOutcome::Success && disk.station() == before,
                            "pending action saves the old document, not its replacement");
                }
                if (!cancelled && action != DocumentAction::Exit) {
                    require(session.station() == (action == DocumentAction::New ? SpaceStation::createDefault() : loaded),
                            "permitted New/Open replaces geometry");
                    require(session.path() == (action == DocumentAction::New ? fs::path{} : openedPath),
                            "permitted New/Open establishes correct identity");
                    require(!session.unsavedChanges() && !session.canUndo() && !session.canRedo(),
                            "permitted replacement resets baseline and history");
                } else {
                    require(session.station() == before, "Cancel/Exit never replaces geometry");
                    require(session.path() == (saved ? savedPath : beforePath), "only successful Save adopts destination");
                    require(session.unsavedChanges() == !saved, "Cancel/Discard do not clear dirty baseline");
                    require(session.canUndo() && session.canRedo(), "Cancel/Exit preserves history");
                    session.redo();
                    require(session.station().deck(0).cell(8, 11) == CellState::Void, "redo content preserved");
                    session.undo();
                    require(session.station() == before, "undo content preserved");
                }
            }
        }
    }
    // Launch, freshly saved and freshly opened documents all bypass the prompt.
    for (int baseline = 0; baseline < 3; ++baseline) {
        for (auto action : actions) {
            EditorSession session;
            GuardDialogs dialogs;
            dialogs.destination = {FileOutcome::Success, savedPath, {}};
            dialogs.source = {FileOutcome::Success, openedPath, {}};
            if (baseline == 1) session.save(dialogs, files);
            if (baseline == 2) session.open(dialogs, files);
            dialogs.opens = dialogs.saves = 0;
            Prompt prompt; // default Cancel would block if erroneously asked
            require(session.request(action, prompt, dialogs, files).outcome == FileOutcome::Success,
                    "clean action proceeds without consent");
            require(prompt.requests == 0 && dialogs.saves == 0, "clean baseline never prompts or saves");
            require(dialogs.opens == (action == DocumentAction::Open ? 1 : 0), "clean Open still selects input");
        }
    }
}

void unsuccessfulGuards(TempFiles& temp) {
    YamlDocumentFiles files;
    const auto savedPath = temp.dir / "blocked-saved.yaml";
    for (auto action : actions) {
        // Native destination cancel/error, failed first write, failed named
        // write, and prompt failure must all prevent every destructive action.
        for (int failure = 0; failure < 5; ++failure) {
            EditorSession session;
            GuardDialogs dialogs;
            Prompt prompt;
            prompt.decision = UnsavedDecision::Save;
            dialogs.destination = {FileOutcome::Success, savedPath, {}};
            dialogs.source = {FileOutcome::Success, temp.dir / "must-not-open.yaml", {}};
            if (failure == 3) session.save(dialogs, files);
            const auto originalPath = session.path();
            session.toggleCell(0, 5, 9);
            session.toggleCell(0, 1, 2);
            session.undo();
            const auto before = session.station();
            if (failure == 0) dialogs.destination = {FileOutcome::Cancelled, {}, {}};
            if (failure == 1) dialogs.destination = {FileOutcome::Failed, {}, "native dialog failure"};
            if (failure == 2) dialogs.destination.path = temp.dir / "nonexistent" / "bad.yaml";
            if (failure == 3) serialization::setTransactionalWriteFailureAfterBytesForTesting(8);
            if (failure == 4) prompt.fail = true;
            const auto result = session.request(action, prompt, dialogs, files);
            serialization::setTransactionalWriteFailureAfterBytesForTesting(0);
            require(result.outcome == (failure == 0 ? FileOutcome::Cancelled : FileOutcome::Failed),
                    "unsuccessful guard blocks pending action with distinct outcome");
            require((failure == 0) == result.diagnostic.empty(), "failed guard reports diagnostic, cancellation does not");
            require(dialogs.opens == 0, "failed save never reaches pending Open");
            require(session.station() == before && session.path() == originalPath && session.unsavedChanges(),
                    "blocked action retains editable unsaved document and identity");
            require(session.canUndo() && session.canRedo(), "blocked action retains history");
            session.redo();
            require(session.station().deck(0).cell(1, 2) == CellState::Void, "blocked action preserves redo content");
            session.undo();
            require(session.station() == before, "blocked action preserves undo content");
        }
    }
    // Consent to discard is not an eager reset. A cancelled/refused Open
    // leaves the current document intact. A preceding Save remains committed.
    for (auto decision : {UnsavedDecision::Save, UnsavedDecision::Discard}) {
        for (int failure = 0; failure < 3; ++failure) {
            EditorSession session;
            GuardDialogs dialogs;
            dialogs.destination = {FileOutcome::Success, savedPath, {}};
            session.save(dialogs, files);
            session.toggleCell(0, 9, 4);
            session.toggleCell(0, 12, 3);
            session.undo();
            const auto before = session.station();
            Prompt prompt;
            prompt.decision = decision;
            dialogs.source = failure == 0 ? FileSelection{FileOutcome::Cancelled, {}, {}} :
                failure == 1 ? FileSelection{FileOutcome::Failed, {}, "native Open failure"} :
                FileSelection{FileOutcome::Success, temp.dir / "refused.yaml", {}};
            writeText(temp.dir / "refused.yaml", "version: 99\n");
            auto result = session.request(DocumentAction::Open, prompt, dialogs, files);
            require(result.outcome == (failure == 0 ? FileOutcome::Cancelled : FileOutcome::Failed), "unsuccessful Open reported");
            require(session.station() == before && session.path() == savedPath, "unsuccessful Open preserves geometry and path after consent");
            require(session.unsavedChanges() == (decision != UnsavedDecision::Save), "only preceding successful Save changes baseline");
            require(session.canUndo() && session.canRedo(), "unsuccessful Open preserves both history branches");
        }
    }
}

void baselineAndInterruptedGesture(TempFiles& temp) {
    EditorSession session;
    YamlDocumentFiles files;
    GuardDialogs dialogs;
    dialogs.destination = {FileOutcome::Success, temp.dir / "branch.yaml", {}};
    Prompt prompt;
    session.toggleCell(0, 2, 3);
    require(session.unsavedChanges(), "edit dirties initial baseline");
    session.undo();
    require(!session.unsavedChanges(), "undo to initial geometry is clean");
    session.redo();
    session.save(dialogs, files);
    require(!session.unsavedChanges(), "Save establishes edited baseline");
    session.undo();
    require(session.unsavedChanges(), "undo across saved baseline is dirty");
    session.toggleCell(0, 7, 8); // different geometry at the same history depth
    require(session.unsavedChanges() && !session.canRedo(), "branch is dirty even at saved history depth");
    session.toggleCell(0, 7, 8);
    session.toggleCell(0, 2, 3);
    require(!session.unsavedChanges(), "equivalent geometry is clean even on another history branch");
    const auto baseline = session.station();
    session.beginStroke(0, 10, 10);
    session.paintStroke(10, 10);
    session.paintStroke(10, 11);
    require(session.unsavedChanges(), "in-flight geometry is already dirty");
    require(session.request(DocumentAction::Exit, prompt, dialogs, files).outcome == FileOutcome::Cancelled,
            "window-close action during stroke can be cancelled");
    require(!session.strokeActive(), "interrupted stroke is finalized before prompting");
    session.undo();
    require(session.station() == baseline && !session.unsavedChanges(), "cancelled close keeps whole interrupted stroke undoable");
    session.redo();
    require(session.unsavedChanges(), "redo interrupted stroke is dirty");
    prompt.decision = UnsavedDecision::Save;
    require(session.request(DocumentAction::Exit, prompt, dialogs, files).outcome == FileOutcome::Success,
            "successful Save authorizes Exit");
    require(!session.unsavedChanges(), "saved interrupted geometry establishes baseline");
    // Save As must update the baseline on a branch without destroying history.
    session.undo();
    dialogs.destination.path = temp.dir / "branch-copy.yaml";
    require(session.saveAs(dialogs, files).outcome == FileOutcome::Success, "Save As on branch succeeds");
    require(session.path() == dialogs.destination.path && !session.unsavedChanges() && session.canRedo(),
            "Save As adopts clean identity and preserves redo");
    session.redo();
    require(session.unsavedChanges(), "redo leaves Save As baseline");
    session.undo();
    require(!session.unsavedChanges(), "undo returns to Save As baseline");
}
}  // namespace

int main() {
    try {
        TempFiles temp;
        roundTrips(temp);
        destinationsAndFailures(temp);
        invalidDocuments(temp);
        guardedActionMatrix(temp);
        unsuccessfulGuards(temp);
        baselineAndInterruptedGesture(temp);
        std::cout << "Persistence consumer passed " << checks << " checks\n";
    } catch (const std::exception& error) {
        serialization::setTransactionalWriteFailureAfterBytesForTesting(0);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
