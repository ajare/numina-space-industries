#include "NativeFileDialogs.h"

#include <nfd.h>

#include <stdexcept>

namespace {
station::editor::FileSelection selection(nfdresult_t result, nfdu8char_t* path) {
    using station::editor::FileOutcome;
    if (result == NFD_CANCEL) return {FileOutcome::Cancelled, {}, {}};
    if (result != NFD_OKAY) {
        const char* error = NFD_GetError();
        return {FileOutcome::Failed, {}, std::string("Native dialog failed: ") +
                (error ? error : "unknown NFD error")};
    }
    // Free NFD's allocation even if path conversion throws.
    struct FreePath {
        nfdu8char_t* value;
        ~FreePath() { NFD_FreePathU8(value); }
    } owned{path};
    return {FileOutcome::Success, std::filesystem::path(reinterpret_cast<const char8_t*>(path)), {}};
}
constexpr nfdu8filteritem_t yamlFilter[] = {{"Installation YAML", "yaml,yml"}};
}  // namespace

NativeFileDialogs::NativeFileDialogs() {
    if (NFD_Init() != NFD_OKAY) {
        const char* error = NFD_GetError();
        throw std::runtime_error(error ? error : "NFD initialization failed");
    }
}

NativeFileDialogs::~NativeFileDialogs() { NFD_Quit(); }

station::editor::FileSelection NativeFileDialogs::openYaml() {
    nfdu8char_t* path = nullptr;
    const auto result = NFD_OpenDialogU8(&path, yamlFilter, 1, nullptr);
    return selection(result, path);
}

station::editor::FileSelection NativeFileDialogs::saveYaml(const std::filesystem::path& current) {
    nfdu8char_t* path = nullptr;
    const auto parent = current.parent_path().u8string();
    const auto name = current.empty() ? std::u8string(u8"station.yaml") : current.filename().u8string();
    const auto result = NFD_SaveDialogU8(&path, yamlFilter, 1,
        parent.empty() ? nullptr : reinterpret_cast<const char*>(parent.c_str()),
        reinterpret_cast<const char*>(name.c_str()));
    return selection(result, path);
}
