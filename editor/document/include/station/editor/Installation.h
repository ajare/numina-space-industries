#pragma once
#include <nsi/core/Installation.h>

namespace station::editor {
using Installation = nsi::core::Installation;
using Level = Installation::Level;
using SectorType = Installation::SectorType;
inline constexpr std::uint32_t kDefaultLevelWidth = 16;
inline constexpr std::uint32_t kDefaultLevelDepth = 16;
}
