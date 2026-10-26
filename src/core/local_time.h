#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

// Unix time now: seconds since 1970-01-01 UTC
std::int64_t unix_now();

// unix_time as a date and time in the computer's time zone
std::tm local_time(std::int64_t unix_time);

// The calendar day of a local_time(), e.g. to count the days between two
// of them by subtracting
std::chrono::sys_days day_of(const std::tm& time);

// time in strftime's format, e.g. "%H:%M" for 14:52. Names of days and
// months are in English, since the app leaves the locale at "C".
std::string format_time(const std::tm& time, const char* format);
