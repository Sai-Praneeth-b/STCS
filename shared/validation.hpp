#pragma once

#include <cstddef>
#include <string>

namespace stcs {

constexpr std::size_t kMaxDisplayNameChars = 24;
constexpr std::size_t kMaxTitleChars = 120;

std::string trim_ascii(const std::string& text);

bool validate_display_name(const std::string& name, std::string& reason);

bool validate_title(const std::string& raw, std::string& normalized, std::string& reason);

bool parse_task_id_text(const std::string& text, long long& task_id, std::string& reason);
bool parse_port_text(const std::string& text, int& port, std::string& reason);

}
