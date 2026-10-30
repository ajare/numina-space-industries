#include <nsi/core/CivilDate.h>

#include <cmath>
#include <cstdio>
#include <limits>

namespace nsi::core {
namespace {
// "YYYY-MM-DD" and nothing else. A wider or narrower string is refused
// rather than interpreted, because a start date read wrong is a clock that
// is wrong for every second of the run.
constexpr std::size_t kIsoDateLength{10};

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// Remainder that follows the floor division the day boundaries are cut on.
// C++'s % truncates toward zero, which would put 1969-12-31T23:59:59Z on
// the epoch's own day.
int64_t floorMod(int64_t value, int64_t modulus) noexcept {
    const int64_t remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

// Days in a month of the proleptic Gregorian calendar, month 1..12. Zero
// for a month that does not exist, so the caller's range check covers both.
int daysInMonth(int month, int year) noexcept {
    static constexpr int kDays[13]{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month != 2) return kDays[month];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
}
} // namespace

std::string isoDateFromUtcSeconds(int64_t seconds) {
    const auto date = civilFromUtcSeconds(seconds);
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", date.year, date.month, date.day);
    return std::string(buffer);
}

std::optional<int64_t> utcSecondsFromIsoDate(std::string_view text) noexcept {
    if (text.size() != kIsoDateLength) return std::nullopt;
    if (text[4] != '-' || text[7] != '-') return std::nullopt;
    for (int index : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!isDigit(text[static_cast<std::size_t>(index)])) return std::nullopt;
    const auto digit = [&text](std::size_t position) { return text[position] - '0'; };
    const int year = digit(0) * 1000 + digit(1) * 100 + digit(2) * 10 + digit(3);
    const int month = digit(5) * 10 + digit(6);
    const int day = digit(8) * 10 + digit(9);
    // A calendar-valid date, not just a shaped string. "2050-02-30" has
    // the right shape and no such day, and accepting it would have the
    // picker open on a month that never had the date it claims.
    if (month < 1 || month > 12) return std::nullopt;
    if (day < 1 || day > daysInMonth(month, year)) return std::nullopt;
    return utcSecondsFromCivil(year, month, day);
}

std::string formatUtcDateTime(int64_t seconds) {
    const int64_t secondOfDay = floorMod(seconds, kSecondsPerDay);
    const auto date = civilFromUtcSeconds(seconds);
    const int hour = static_cast<int>(secondOfDay / 3600);
    const int minute = static_cast<int>((secondOfDay % 3600) / 60);
    const int second = static_cast<int>(secondOfDay % 60);
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d %02d:%02d:%02d UTC", date.year, date.month,
                  date.day, hour, minute, second);
    return std::string(buffer);
}

std::string formatUtcDateTimeAtStartPlus(int64_t startUtcS, double elapsedSeconds) {
    // No clock yet, or a clock that has not started: the authored instant
    // is the whole of the truth, and saying so beats inventing a today.
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0) return formatUtcDateTime(startUtcS);
    // Floored, not rounded. A clock that shows a second it has not reached
    // yet reads as running backwards on the frames before it, and the
    // displayed second is a floor of the simulated one either way.
    const double floored = std::floor(elapsedSeconds);
    // Saturating rather than wrapping: past what an int64 can hold the run
    // has outlasted the calendar, and the last representable instant is
    // closer to the truth than a negative one.
    const int64_t magnitude = startUtcS < 0 ? -startUtcS : startUtcS;
    const double limit = static_cast<double>(std::numeric_limits<int64_t>::max()) -
                        static_cast<double>(magnitude) - 1.0;
    if (floored >= limit) return formatUtcDateTime(std::numeric_limits<int64_t>::max());
    return formatUtcDateTime(startUtcS + static_cast<int64_t>(floored));
}

} // namespace nsi::core
