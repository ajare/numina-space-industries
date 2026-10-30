#include <nsi/core/CivilDate.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace nsi::core;

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

// The epoch, and the two directions agreeing about it.
void theEpochIsTheEpoch() {
    require(daysFromCivil(1970, 1, 1) == 0, "1970-01-01 is not day zero");
    require(civilFromDays(0) == (CivilDate{1970, 1, 1}), "day zero is not 1970-01-01");
    require(utcSecondsFromCivil(1970, 1, 1) == 0, "the epoch instant is not zero seconds");
    require(civilFromUtcSeconds(0) == (CivilDate{1970, 1, 1}), "epoch seconds are not the epoch date");
}

// Anchors checked against an independent calendar, so the algorithm is
// tied to the real Gregorian days and not just to itself.
void knownDatesLandWhereTheCalendarSays() {
    require(utcSecondsFromCivil(2050, 1, 1) == 2524608000, "2050-01-01 is not 2524608000");
    require(utcSecondsFromCivil(2000, 2, 29) == 951782400, "2000-02-29 is not 951782400");
    require(utcSecondsFromCivil(2200, 12, 31) == 7289568000, "2200-12-31 is not 7289568000");
    require(civilFromUtcSeconds(2524608000) == (CivilDate{2050, 1, 1}),
            "2524608000 does not read back as 2050-01-01");
}

// The default the Editor opens on: 1st January 2050, midnight.
void theDefaultStartIsMidnightOnTheFirstOfJanuary2050() {
    require(kDefaultStartDateUtcS == 2524608000, "the default start date is not 2050-01-01");
    require(kDefaultStartDateUtcS % kSecondsPerDay == 0, "the default start is not midnight");
    require(isoDateFromUtcSeconds(kDefaultStartDateUtcS) == "2050-01-01",
            "the default start does not read as 2050-01-01");
}

// Every day of the supported range round-trips through both directions,
// and consecutive days are exactly one day apart. A leap-year bug or an
// off-by-one in the month table shows up immediately here rather than on
// the one date somebody happens to try.
void everyDayInTheRangeRoundTrips() {
    const int64_t first = daysFromCivil(kMinAuthoredYear, 1, 1);
    const int64_t last = daysFromCivil(kMaxAuthoredYear, 12, 31);
    require(last > first, "the supported range is empty");
    int64_t previous = first - 1;
    for (int64_t day = first; day <= last; ++day) {
        const auto date = civilFromDays(day);
        require(daysFromCivil(date.year, date.month, date.day) == day,
                "a day in the supported range did not round-trip");
        require(day - previous == 1, "consecutive days are not one day apart");
        previous = day;
    }
}

// February is the whole reason this needs testing.
void leapYearsAreRight() {
    require(daysFromCivil(2000, 3, 1) - daysFromCivil(2000, 2, 1) == 29,
            "2000 was not a leap year (divisible by 400)");
    require(daysFromCivil(1900, 3, 1) - daysFromCivil(1900, 2, 1) == 28,
            "1900 was treated as a leap year (divisible by 100, not 400)");
    require(daysFromCivil(2100, 3, 1) - daysFromCivil(2100, 2, 1) == 28,
            "2100 was treated as a leap year");
    require(daysFromCivil(2024, 3, 1) - daysFromCivil(2024, 2, 1) == 29,
            "2024 was not treated as a leap year");
    require(daysFromCivil(2023, 3, 1) - daysFromCivil(2023, 2, 1) == 28,
            "2023 was treated as a leap year");
}

void isoDatesParse() {
    require(utcSecondsFromIsoDate("2050-01-01").value_or(-1) == 2524608000,
            "2050-01-01 did not parse");
    require(utcSecondsFromIsoDate("1970-01-01").value_or(-1) == 0, "the epoch did not parse");
    require(utcSecondsFromIsoDate("2000-02-29").has_value(), "a real leap day was refused");
    require(utcSecondsFromIsoDate("2200-12-31").has_value(), "the last supported day was refused");
}

// Refused rather than guessed. Each of these has a reading a lenient parser
// would take, and each of those readings is a wrong clock.
void malformedDatesAreRefused() {
    const std::vector<std::string> rejected{
        "",
        "2050",
        "2050-01",
        "2050-01-01 00:00",              // a time is not wanted and not accepted
        "2050/01/01",                    // wrong separators
        "2050-1-1",                      // not zero padded
        "20500-01-01",
        "20X0-01-01",
        "2050-0X-01",
        "2050-01-0X",
        "2050-00-01",                    // month zero
        "2050-13-01",                    // month thirteen
        "2050-01-00",                    // day zero
        "2050-01-32",                    // no such day
        "2049-02-29",                    // 2049 is not a leap year
        "2100-02-29",                    // divisible by 100, not by 400
        "2050-04-31",                    // April has thirty days
        " 2050-01-01",
        "2050-01-01 ",
    };
    for (const auto& text : rejected)
        require(!utcSecondsFromIsoDate(text).has_value(),
                ("a malformed date was accepted: \"" + text + "\"").c_str());
}

void isoDatesFormat() {
    require(isoDateFromUtcSeconds(2524608000) == "2050-01-01", "2050-01-01 did not format");
    require(isoDateFromUtcSeconds(0) == "1970-01-01", "the epoch did not format");
    require(isoDateFromUtcSeconds(-1) == "1969-12-31",
            "a second before the epoch fell on the wrong day - the division is truncating, not flooring");
    require(isoDateFromUtcSeconds(-kSecondsPerDay) == "1969-12-31",
            "exactly one day before the epoch read as the epoch");
}

// The running clock. The authored start is always midnight, so the time
// of day only ever comes from elapsed simulated seconds.
void theRunningClockReadsDateAndTime() {
    require(formatUtcDateTime(2524608000) == "2050-01-01 00:00:00 UTC",
            "midnight on the default start did not format");
    require(formatUtcDateTime(2524631652) == "2050-01-01 06:34:12 UTC",
            "a time of day did not format");
    require(formatUtcDateTime(2524608000 + kSecondsPerDay - 1) == "2050-01-01 23:59:59 UTC",
            "the last second of the day did not format");
    require(formatUtcDateTime(2524608000 + kSecondsPerDay) == "2050-01-02 00:00:00 UTC",
            "crossing midnight did not roll the date");
    // A run of days rolls the calendar the way a calendar does.
    require(formatUtcDateTime(2524608000 + 30 * kSecondsPerDay + 43200) ==
                "2050-01-31 12:00:00 UTC",
            "thirty days and a half day did not land on the thirty-first");
}

// The composition the Editor actually calls: authored start plus simulated
// seconds, with the rounding to a displayed second happening here.
void theClockFollowsElapsedSimulatedSeconds() {
    const int64_t start = kDefaultStartDateUtcS;
    require(formatUtcDateTimeAtStartPlus(start, 0.0) == "2050-01-01 00:00:00 UTC",
            "a run that has not started does not read the authored instant");
    require(formatUtcDateTimeAtStartPlus(start, 1.0) == "2050-01-01 00:00:01 UTC",
            "one simulated second did not advance the clock");
    require(formatUtcDateTimeAtStartPlus(start, 3599.9) == "2050-01-01 00:59:59 UTC",
            "the hour did not count up");
    require(formatUtcDateTimeAtStartPlus(start, 86400.0) == "2050-01-02 00:00:00 UTC",
            "a day of simulation did not roll the date");
    // Floored, not rounded: 0.9 s has not reached the first second yet.
    require(formatUtcDateTimeAtStartPlus(start, 0.9) == "2050-01-01 00:00:00 UTC",
            "the displayed second was rounded up before it was reached");
    // A clock with nothing in it reads the start rather than a blank or a
    // nonsense date.
    require(formatUtcDateTimeAtStartPlus(start, kNaN) == "2050-01-01 00:00:00 UTC",
            "a NaN elapsed produced a date nobody set");
    require(formatUtcDateTimeAtStartPlus(start, -5.0) == "2050-01-01 00:00:00 UTC",
            "a negative elapsed ran the clock backwards");
    // A run long enough to overflow the storage saturates at the last
    // representable instant instead of wrapping into the past.
    const auto saturated = formatUtcDateTimeAtStartPlus(
        start, std::numeric_limits<double>::max());
    require(saturated == formatUtcDateTime(std::numeric_limits<int64_t>::max()),
            "an absurd elapsed time wrapped the clock around instead of saturating");
}
} // namespace

int main() {
    try {
        theEpochIsTheEpoch();
        knownDatesLandWhereTheCalendarSays();
        theDefaultStartIsMidnightOnTheFirstOfJanuary2050();
        everyDayInTheRangeRoundTrips();
        leapYearsAreRight();
        isoDatesParse();
        malformedDatesAreRefused();
        isoDatesFormat();
        theRunningClockReadsDateAndTime();
        theClockFollowsElapsedSimulatedSeconds();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "civil date ok\n";
    return 0;
}
