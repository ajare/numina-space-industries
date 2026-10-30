#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nsi::core {

// Calendar arithmetic for the authored mission start date.
//
// Deliberately not libc. `timegm` is the function that would do this and it
// does not exist on MSVC (which calls it `_mkgmtime`), while `gmtime`
// returns a pointer to a static buffer that every call overwrites. The
// algorithms below are integer-only, exact over the whole range a person can
// type into a date field, and free of any global state, so the same
// arithmetic that formats a save file can be asserted in a test without a
// locale, a timezone or a thread in sight.
//
// The calendar is the proleptic Gregorian one, which is what the date
// picker draws and what ISO 8601 means.

inline constexpr int64_t kSecondsPerDay{86400};
// The epoch every stored instant is measured from: 1970-01-01T00:00:00Z.
inline constexpr int kUnixEpochYear{1970};
// The range an authored start date may land in. The lower bound is the
// epoch itself, so every stored instant is non-negative and nothing
// downstream has to argue about dates from before the calendar the
// storage assumes. The upper bound is a century of headroom past the
// default: generous for a mission, and it keeps the year inside four
// digits. The Editor's date picker is configured to exactly this range so
// what cannot be picked is also what will not validate.
inline constexpr int kMinAuthoredYear{1970};
inline constexpr int kMaxAuthoredYear{2200};

// A calendar date. Month is 1..12, the way a person reads it, not the
// 0-based `tm_mon` the widget works in.
struct CivilDate {
    int year{};
    int month{};
    int day{};
    bool operator==(const CivilDate&) const = default;
};

// Days from 1970-01-01 to year-month-day, negative for dates before the
// epoch. Howard Hinnant's `days_from_civil`: exact integer arithmetic, no
// floating point anywhere near it.
constexpr int64_t daysFromCivil(int year, int month, int day) noexcept {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);  // [0, 399]
    // The year is counted from March here, so Jan/Feb are pushed to the
    // end of the previous year's count.
    const unsigned shiftedMonth = static_cast<unsigned>(month + (month > 2 ? -3 : 9));  // [0, 11]
    const unsigned dayOfYear =
        (153u * shiftedMonth + 2u) / 5u + static_cast<unsigned>(day) - 1u;  // [0, 365]
    const unsigned dayOfEra =
        yearOfEra * 365u + yearOfEra / 4u - yearOfEra / 100u + dayOfYear;  // [0, 146096]
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

// The inverse: the calendar date `days` after 1970-01-01.
constexpr CivilDate civilFromDays(int64_t days) noexcept {
    const int64_t shifted = days + 719468;
    const int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(shifted - era * 146097);  // [0, 146096]
    const unsigned yearOfEra =
        (dayOfEra - dayOfEra / 1460u + dayOfEra / 36524u - dayOfEra / 146096u) /
        365u;  // [0, 399]
    const int64_t year = static_cast<int64_t>(yearOfEra) + era * 400;
    const unsigned dayOfYear = dayOfEra - (365u * yearOfEra + yearOfEra / 4u - yearOfEra / 100u);
    const unsigned shiftedMonth = (5u * dayOfYear + 2u) / 153u;  // [0, 11]
    const unsigned day = dayOfYear - (153u * shiftedMonth + 2u) / 5u + 1u;  // [1, 31]
    // The year runs March to February in this counting, so January and
    // February belong to the year after the one the arithmetic arrived at.
    const int month = static_cast<int>(shiftedMonth) + (shiftedMonth < 10u ? 3 : -9);  // [1, 12]
    return CivilDate{static_cast<int>(year + (month <= 2 ? 1 : 0)), month, static_cast<int>(day)};
}

// Midnight UTC on a calendar date, as seconds since the epoch. Every
// authored start date lands on one of these, because the author picks a
// day and never an hour.
constexpr int64_t utcSecondsFromCivil(const CivilDate& date) noexcept {
    return daysFromCivil(date.year, date.month, date.day) * kSecondsPerDay;
}

// Midnight UTC on a year/month/day, month 1..12.
constexpr int64_t utcSecondsFromCivil(int year, int month, int day) noexcept {
    return utcSecondsFromCivil(CivilDate{year, month, day});
}

// The calendar date an instant falls on. Divides toward negative infinity,
// so 1969-12-31T23:59:59Z is the day before the epoch and not the day of
// it, which truncation toward zero would have got wrong.
constexpr CivilDate civilFromUtcSeconds(int64_t seconds) noexcept {
    const int64_t days = seconds >= 0 ? seconds / kSecondsPerDay
                                     : (seconds - (kSecondsPerDay - 1)) / kSecondsPerDay;
    return civilFromDays(days);
}

// The date the Editor starts a new document on: 1st January 2050 at
// midnight UTC. Computed rather than written as a literal, so it cannot
// drift away from the arithmetic it is defined in terms of.
inline constexpr int64_t kDefaultStartDateUtcS{utcSecondsFromCivil(2050, 1, 1)};

// "2050-01-01". Always zero padded, always UTC.
std::string isoDateFromUtcSeconds(int64_t seconds);

// The inverse. Returns nothing rather than guessing when the text is not a
// well formed ISO date: a start date read wrong is a clock that is wrong
// for the whole run, and silently rounding "2050-1-1" or accepting
// "2050-02-30" would both be lies.
std::optional<int64_t> utcSecondsFromIsoDate(std::string_view text) noexcept;

// "2050-01-01 06:34:12 UTC" - the running clock. Takes the whole instant,
// so it reads whatever day and time the simulation has reached rather than
// only the midnight an author could pick.
std::string formatUtcDateTime(int64_t seconds);

// The clock a run shows `elapsed` simulated seconds after its authored
// start. Simulated seconds are a double, so this is where the rounding to
// a whole displayed second happens; a non-finite elapsed reads as the
// start itself rather than as a date nobody set.
std::string formatUtcDateTimeAtStartPlus(int64_t startUtcS, double elapsedSeconds);

} // namespace nsi::core
