#pragma once

#include <cstdint>
#include <ctime>
#include <optional>

namespace station::editor {

// The authored mission start date, and the picker that writes it.
//
// The widget works in a `tm` and can only choose a day - there is no
// time-of-day control, which is exactly the shape the document wants,
// since a start date is always midnight UTC. The time fields are zeroed
// on the way back out regardless, because the picker's "today" button
// fills them from the wall clock and that hour is nobody's business.
//
// The conversions go through nsi::core::CivilDate rather than a libc
// break-down, so no timezone and no locale gets a vote on what day the
// document says the mission starts.

// The width the picker's field is drawn at, matching the sky fields in
// the same panel so the column lines up with everything above it.
inline constexpr float kStartDateFieldWidthPx{128.0f};

// The stored instant as the `tm` the picker reads.
std::tm tmFromUtcSeconds(std::int64_t seconds) noexcept;

// The picker's `tm` back to a stored instant, forced to midnight UTC.
// Only the year, month and day are read; everything else is discarded.
std::int64_t utcSecondsFromTm(const std::tm& picked) noexcept;

// The widget. Returns the newly chosen instant when the author changed
// the date, and nothing when they did not.
std::optional<std::int64_t> pickStartDate(const char* label, std::int64_t currentUtcS);

} // namespace station::editor
