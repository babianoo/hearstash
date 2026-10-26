#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

// Spans of time the history window's sidebar groups searches by, newest
// first, like a browser's history. Every time is in exactly one.
enum class TimeRange : size_t {
    Today,
    Yesterday,
    // 2 to 6 days ago
    Last7Days,
    // The rest of this month
    ThisMonth,
    // The 5 months before this one, each by its name
    LastMonth,
    TwoMonthsAgo,
    ThreeMonthsAgo,
    FourMonthsAgo,
    FiveMonthsAgo,
    // Everything before those
    Older,
};
inline constexpr size_t TIME_RANGE_COUNT = static_cast<size_t>(TimeRange::Older) + 1;

// The range time (Unix time) is in, as of now (a local_time()). A time
// after now, e.g. from before the clock was turned back, counts as Today.
TimeRange range_of(std::int64_t time, const std::tm& now);

// As the sidebar shows it, e.g. "Today", or for a month "September", with
// its year if that isn't now's: "December 2025"
std::string range_name(TimeRange range, const std::tm& now);
