#include "StartDatePicker.h"

#include "ImGuiDatePicker.hpp"

#include <nsi/core/CivilDate.h>

// The picker's selectable years and the years core will accept have to be
// the same numbers, or the author can pick a date the document then
// refuses. Both are set from the same pair of constants in spirit; this
// makes the pairing a compile error rather than a support ticket.
static_assert(IMGUI_DATEPICKER_YEAR_MIN == nsi::core::kMinAuthoredYear,
              "The date picker's minimum year is not the range core validates against");
static_assert(IMGUI_DATEPICKER_YEAR_MAX == nsi::core::kMaxAuthoredYear,
              "The date picker's maximum year is not the range core validates against");

namespace station::editor {

std::tm tmFromUtcSeconds(std::int64_t seconds) noexcept {
    const auto date = nsi::core::civilFromUtcSeconds(seconds);
    std::tm out{};
    out.tm_year = date.year - 1900;
    out.tm_mon = date.month - 1;   // the picker's tm_mon is 0-based
    out.tm_mday = date.day;
    out.tm_isdst = -1;             // no DST opinion is being offered
    return out;
}

std::int64_t utcSecondsFromTm(const std::tm& picked) noexcept {
    // Day only. The hour, minute and second the tm may be carrying - the
    // picker's "today" button brings them in from the wall clock - are
    // dropped, because a start date that inherited a wall-clock hour is
    // a start nobody chose.
    return nsi::core::utcSecondsFromCivil(picked.tm_year + 1900, picked.tm_mon + 1, picked.tm_mday);
}

std::optional<std::int64_t> pickStartDate(const char* label, std::int64_t currentUtcS) {
    std::tm picked = tmFromUtcSeconds(currentUtcS);
    // Keep the label aligned with the sky fields above.
    ImGui::SetNextItemWidth(128.0f);
    if (!ImGui::DatePicker(label, picked, false, 128.0f)) return std::nullopt;
    return utcSecondsFromTm(picked);
}

} // namespace station::editor
