#include "StartDatePicker.h"

#include <nsi/core/CivilDate.h>

#include <iostream>
#include <stdexcept>

using namespace station::editor;
namespace core = nsi::core;

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

// The stored instant arrives at the picker as the calendar day it means,
// in the picker's own conventions: tm_mon 0-based, tm_year off 1900.
void theStoredInstantReadsAsThePickerSeesIt() {
    const auto picked = tmFromUtcSeconds(core::kDefaultStartDateUtcS);
    require(picked.tm_year == 2050 - 1900, "the picker year is not 2050");
    require(picked.tm_mon == 0, "the picker month is not 0-based January");
    require(picked.tm_mday == 1, "the picker day is not the 1st");
    require(utcSecondsFromTm(picked) == core::kDefaultStartDateUtcS,
            "the default start date did not survive the round trip through the picker");
}

// The picker's "today" button fills the hour, minute and second from the
// wall clock. None of them may reach the document: the authored start is
// midnight and stays midnight whatever the widget was holding.
void theTimeOfDayIsDroppedOnTheWayBackOut() {
    const auto base = tmFromUtcSeconds(core::kDefaultStartDateUtcS);
    for (const int hour : {0, 7, 13, 23})
        for (const int minute : {0, 37, 59})
            for (const int second : {0, 30, 59}) {
                std::tm late = base;
                late.tm_hour = hour;
                late.tm_min = minute;
                late.tm_sec = second;
                require(utcSecondsFromTm(late) == core::kDefaultStartDateUtcS,
                        "a time of day leaked into the authored start date");
            }
    // And the day the author did pick still arrives, with the time gone.
    std::tm moved = base;
    moved.tm_mday = 17;
    moved.tm_hour = 21;
    moved.tm_min = 45;
    require(utcSecondsFromTm(moved) == core::utcSecondsFromCivil(2050, 1, 17),
            "the picked day was lost along with the time");
}

// Every day the picker can offer round-trips through both conversions.
// February 29 in a leap year is the one that would show a broken month
// table, and it is in here along with everything else.
void everyPickableDayRoundTrips() {
    const int64_t first = core::daysFromCivil(core::kMinAuthoredYear, 1, 1);
    const int64_t last = core::daysFromCivil(core::kMaxAuthoredYear, 12, 31);
    require(last > first, "the pickable range is empty");
    for (int64_t day = first; day <= last; ++day) {
        const int64_t seconds = day * core::kSecondsPerDay;
        require(utcSecondsFromTm(tmFromUtcSeconds(seconds)) == seconds,
                "a pickable day did not round-trip through the picker's tm");
    }
    // Spot-check the leap day the range contains, since that is where a
    // month table goes wrong and nowhere else looks wrong.
    const int64_t leapDay = core::utcSecondsFromCivil(2000, 2, 29);
    const auto picked = tmFromUtcSeconds(leapDay);
    require(picked.tm_mon == 1 && picked.tm_mday == 29, "the leap day was not February 29th");
    require(utcSecondsFromTm(picked) == leapDay, "the leap day did not round-trip");
}

// The picker's bounds and core's bounds are the same numbers, asserted at
// compile time in the module; this checks the two ends of that range are
// actually reachable and land where both sides expect.
void theEndsOfTheRangeAreBothEnds() {
    const auto low = tmFromUtcSeconds(core::utcSecondsFromCivil(core::kMinAuthoredYear, 1, 1));
    require(low.tm_year == core::kMinAuthoredYear - 1900, "the low end of the range is wrong");
    const auto high = tmFromUtcSeconds(core::utcSecondsFromCivil(core::kMaxAuthoredYear, 12, 31));
    require(high.tm_year == core::kMaxAuthoredYear - 1900, "the high end of the range is wrong");
    require(high.tm_mon == 11 && high.tm_mday == 31, "the high end is not December 31st");
    require(utcSecondsFromTm(low) == core::utcSecondsFromCivil(core::kMinAuthoredYear, 1, 1),
            "the low end did not round-trip");
    require(utcSecondsFromTm(high) == core::utcSecondsFromCivil(core::kMaxAuthoredYear, 12, 31),
            "the high end did not round-trip");
}
} // namespace

int main() {
    try {
        theStoredInstantReadsAsThePickerSeesIt();
        theTimeOfDayIsDroppedOnTheWayBackOut();
        everyPickableDayRoundTrips();
        theEndsOfTheRangeAreBothEnds();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "start date picker ok\n";
    return 0;
}
