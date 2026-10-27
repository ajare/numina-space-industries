#pragma once

#include <filesystem>

namespace station::editor {

enum class DocumentAction { New, Open, Exit };
enum class UnsavedDecision { Save, Discard, Cancel };

// Synchronous external interaction. Both exit sources use DocumentAction::Exit.
// Closing/dismissing the prompt must mean Cancel, never Discard.
class UnsavedChangesPrompt {
public:
    virtual ~UnsavedChangesPrompt() = default;
    virtual UnsavedDecision confirm(DocumentAction action,
                                    const std::filesystem::path& path) = 0;
};

}  // namespace station::editor
