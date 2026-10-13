#pragma once

#include <string>
#include <vector>

#include "shared/json.hpp"
#include "shared/protocol.hpp"

namespace stcs {

constexpr std::size_t kScreenWidth = 80;

std::vector<std::string> wrap_text(const std::string& text, std::size_t width);

bool task_from_json(const Json& json, Task& task);

std::string format_task_table(const std::vector<Task>& tasks, const std::string& filter);

std::string format_task_detail(const Task& task);

std::string format_error(const std::string& code, const std::string& message);

std::string format_local_help();

std::string format_server_help(const Json& commands);

std::string wrap_with_indent(const std::string& text, const std::string& first_prefix,
                             const std::string& next_prefix);

}
