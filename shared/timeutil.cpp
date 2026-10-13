// timeutil.cpp -- see timeutil.hpp.
#include "shared/timeutil.hpp"

namespace stcs {

std::string format_utc_timestamp(std::time_t when) {
    std::tm utc{};
    gmtime_r(&when, &utc);  // thread-safe variant, ready for Phase 3
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string utc_timestamp_now() {
    return format_utc_timestamp(std::time(nullptr));
}

}  // namespace stcs
