#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stcs {

bool decode_utf8(const std::string& s, std::size_t& pos, std::uint32_t& code_point);

bool is_valid_utf8(const std::string& s);

std::size_t utf8_length(const std::string& s);

void append_utf8(std::string& out, std::uint32_t code_point);

std::string utf8_truncate(const std::string& s, std::size_t max_chars);

bool is_control_code_point(std::uint32_t code_point);

}
