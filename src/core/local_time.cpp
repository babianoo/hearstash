#include "core/local_time.h"

std::int64_t unix_now(){
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::tm local_time(std::int64_t unix_time){
    const auto t = static_cast<std::time_t>(unix_time);
    // Not std::localtime, which returns a buffer shared by the whole program
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::chrono::sys_days day_of(const std::tm& time){
    using namespace std::chrono;
    return sys_days(year(time.tm_year + 1900) / month(time.tm_mon + 1) / day(time.tm_mday));
}

std::string format_time(const std::tm& time, const char* format){
    char buf[64];
    // 0 if it doesn't fit, which none of the app's formats come near
    const size_t len = std::strftime(buf, sizeof(buf), format, &time);
    return std::string(buf, len);
}
