// display.hpp -- plain-text formatting of server data for the terminal.
//
// Pure functions: they take data and return text, so they are unit-tested
// without a terminal. Every returned line fits in 80 columns and no
// information depends on colour (NFR-16).
#pragma once

#include <string>
#include <vector>

#include "shared/json.hpp"
#include "shared/protocol.hpp"

namespace stcs {

constexpr std::size_t kScreenWidth = 80;

// Word-wraps `text` to `width` columns (columns = Unicode code points).
// Words longer than `width` are split. Always returns at least one line.
std::vector<std::string> wrap_text(const std::string& text, std::size_t width);

// Rebuilds a Task from the JSON object in a response. Returns false if any
// field is missing or has the wrong type (the client then reports an
// unexpected server response instead of showing partial data).
bool task_from_json(const Json& json, Task& task);

// The task list as an aligned table (ID, PRIO, STATUS, OWNER, TITLE) with a
// trailing count line. `filter` (e.g. "OPEN") only changes the empty message.
std::string format_task_table(const std::vector<Task>& tasks, const std::string& filter);

// One task as labelled lines (used after add / claim / release / done).
std::string format_task_detail(const Task& task);

// "Error [CODE]: message" followed by a one-line hint for that code.
std::string format_error(const std::string& code, const std::string& message);

// The local command list for the `help` command.
std::string format_local_help();

// The server's HELP reply (array of {name, summary, required_fields}).
std::string format_server_help(const Json& commands);

// Word-wrapped text with a hanging indent for the continuation lines.
std::string wrap_with_indent(const std::string& text, const std::string& first_prefix,
                             const std::string& next_prefix);

}  // namespace stcs
