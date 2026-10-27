#pragma once

#include "station/editor/DocumentIO.h"

// NFD owns no document state. A scoped instance brackets its lifecycle.
class NativeFileDialogs final : public station::editor::FileDialogs {
public:
    NativeFileDialogs();
    ~NativeFileDialogs() override;
    NativeFileDialogs(const NativeFileDialogs&) = delete;
    NativeFileDialogs& operator=(const NativeFileDialogs&) = delete;

    station::editor::FileSelection openYaml() override;
    station::editor::FileSelection saveYaml(const std::filesystem::path& current) override;
};
