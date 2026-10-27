#pragma once

#include "station/editor/SpaceStation.h"

#include <filesystem>
#include <string>

namespace station::editor {

enum class FileOutcome { Success, Cancelled, Failed };

struct FileResult {
    FileOutcome outcome;
    std::string diagnostic;
};

struct FileSelection {
    FileOutcome outcome;
    std::filesystem::path path;
    std::string diagnostic;
};

// Native UI and storage are external interactions, not session state.
class FileDialogs {
public:
    virtual ~FileDialogs() = default;
    virtual FileSelection openYaml() = 0;
    virtual FileSelection saveYaml(const std::filesystem::path& current) = 0;
};

class DocumentFiles {
public:
    virtual ~DocumentFiles() = default;
    // Throw with a useful diagnostic on failure. Read validates before returning;
    // write must not expose a partially written destination.
    virtual SpaceStation read(const std::filesystem::path& path) = 0;
    virtual void write(const std::filesystem::path& path,
                       const SpaceStation& station) = 0;
};

class YamlDocumentFiles final : public DocumentFiles {
public:
    SpaceStation read(const std::filesystem::path& path) override;
    void write(const std::filesystem::path& path,
               const SpaceStation& station) override;
};

}  // namespace station::editor
