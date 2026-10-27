// Cell state vocabulary for Editor-authored geometry.
//
// Filled means station floor exists at the position; Void means empty space
// with no floor. Neither carries material, equipment, or simulation meaning.
#pragma once

#include <cstdint>

namespace station::editor {

enum class CellState : std::uint8_t {
    Void,
    Filled,
};

// The only Deck dimensions this milestone creates and edits. The domain allows
// any rectangular Deck; this is a milestone constraint, not a redefinition.
inline constexpr std::uint32_t kDefaultDeckWidth = 16;
inline constexpr std::uint32_t kDefaultDeckHeight = 16;

}  // namespace station::editor
