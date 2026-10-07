// timeutil.hpp -- ISO-8601 UTC timestamps (Phase 1, 7.2: 2026-03-14T14:22:07Z).
#pragma once

#include <ctime>
#include <string>

namespace stcs {

std::string format_utc_timestamp(std::time_t when);
std::string utc_timestamp_now();

}  // namespace stcs
