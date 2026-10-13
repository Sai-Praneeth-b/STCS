#pragma once

#include <ctime>
#include <string>

namespace stcs {

std::string format_utc_timestamp(std::time_t when);
std::string utc_timestamp_now();

}
