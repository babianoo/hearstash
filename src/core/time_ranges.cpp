#include "core/time_ranges.h"

#include "core/local_time.h"

// Months since year 0, so two can be subtracted across a new year
static int month_number(const std::tm& time){
    return (time.tm_year + 1900) * 12 + time.tm_mon;
}

TimeRange range_of(std::int64_t time, const std::tm& now){
    const std::tm t = local_time(time);
    const auto days_ago = (day_of(now) - day_of(t)).count();
    if (days_ago <= 0) return TimeRange::Today;
    if (days_ago == 1) return TimeRange::Yesterday;
    if (days_ago < 7) return TimeRange::Last7Days;

    const int months_ago = month_number(now) - month_number(t);
    if (months_ago == 0) return TimeRange::ThisMonth;
    if (months_ago <= 5) return static_cast<TimeRange>(static_cast<size_t>(TimeRange::ThisMonth) + months_ago);
    return TimeRange::Older;
}

std::string range_name(TimeRange range, const std::tm& now){
    switch (range){
    case TimeRange::Today: return "Today";
    case TimeRange::Yesterday: return "Yesterday";
    case TimeRange::Last7Days: return "Last 7 days";
    case TimeRange::ThisMonth: return "This month";
    case TimeRange::Older: return "Older";
    default: break;
    }

    // One of the months before this one
    const int months_ago = static_cast<int>(range) - static_cast<int>(TimeRange::ThisMonth);
    const int month = month_number(now) - months_ago;
    std::tm first{};
    first.tm_year = month / 12 - 1900;
    first.tm_mon = month % 12;
    first.tm_mday = 1;
    return format_time(first, first.tm_year == now.tm_year ? "%B" : "%B %Y");
}
