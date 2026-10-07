// display.cpp -- see display.hpp.
#include "client/display.hpp"

#include <algorithm>
#include <sstream>

#include "shared/utf8.hpp"

namespace stcs {

namespace {

// Pads `text` with spaces on the right to `width` columns.
std::string pad_right(const std::string& text, std::size_t width) {
    const std::size_t length = utf8_length(text);
    return length >= width ? text : text + std::string(width - length, ' ');
}

std::string pad_left(const std::string& text, std::size_t width) {
    const std::size_t length = utf8_length(text);
    return length >= width ? text : std::string(width - length, ' ') + text;
}

constexpr std::size_t kPriorityWidth = 6;   // "MEDIUM"
constexpr std::size_t kStatusWidth = 7;     // "CLAIMED"
constexpr std::size_t kOwnerWidth = 14;

// Owners longer than the column are cut with a trailing '~'.
std::string fit_owner(const std::string& owner) {
    if (utf8_length(owner) <= kOwnerWidth) return owner;
    return utf8_truncate(owner, kOwnerWidth - 1) + "~";
}

const char* hint_for(const std::string& code) {
    if (code == "NOT_FOUND")         return "Use 'list' to see which tasks exist.";
    if (code == "CONFLICT")          return "Use 'list' to see the current owner and status.";
    if (code == "MISSING_FIELD" || code == "INVALID_FIELD")
                                     return "Check the value and try again.";
    if (code == "UNKNOWN_COMMAND")   return "Type 'help' to see the supported commands.";
    if (code == "INVALID_STATE")     return "The command is not allowed in the current session state.";
    if (code == "INVALID_MESSAGE")   return "The server could not understand the message.";
    if (code == "INVALID_VERSION")   return "The client and server use different protocol versions.";
    if (code == "NAME_IN_USE")       return "Choose a different display name.";
    if (code == "MESSAGE_TOO_LARGE") return "Shorten the input and try again.";
    if (code == "SESSION_ERROR")     return "The session has ended; connect again.";
    if (code == "SERVER_ERROR")      return "The request could not be completed; try again later.";
    return "";
}

}  // namespace

std::vector<std::string> wrap_text(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    std::string current;
    std::size_t current_length = 0;

    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::size_t word_length = utf8_length(word);
        // A word wider than a whole line is split into line-sized pieces.
        while (word_length > width) {
            if (current_length > 0) {
                lines.push_back(current);
                current.clear();
                current_length = 0;
            }
            const std::string head = utf8_truncate(word, width);
            lines.push_back(head);
            word = word.substr(head.size());
            word_length = utf8_length(word);
        }
        if (word.empty()) continue;
        if (current_length == 0) {
            current = word;
            current_length = word_length;
        } else if (current_length + 1 + word_length <= width) {
            current += " " + word;
            current_length += 1 + word_length;
        } else {
            lines.push_back(current);
            current = word;
            current_length = word_length;
        }
    }
    if (current_length > 0 || lines.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::string wrap_with_indent(const std::string& text, const std::string& first_prefix,
                             const std::string& next_prefix) {
    const std::size_t prefix_width = std::max(utf8_length(first_prefix), utf8_length(next_prefix));
    const std::vector<std::string> lines = wrap_text(text, kScreenWidth - prefix_width);
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += (i == 0 ? first_prefix : next_prefix) + lines[i] + "\n";
    }
    return out;
}

bool task_from_json(const Json& json, Task& task) {
    if (!json.is_object()) return false;
    const Json* id = json.find("task_id");
    const Json* title = json.find("title");
    const Json* priority = json.find("priority");
    const Json* status = json.find("status");
    const Json* claimed_by = json.find("claimed_by");
    const Json* created_by = json.find("created_by");
    const Json* created_at = json.find("created_at");
    if (!id || !id->is_int() || !title || !title->is_string() || !priority ||
        !priority->is_string() || !status || !status->is_string() || !claimed_by ||
        !created_by || !created_by->is_string() || !created_at || !created_at->is_string()) {
        return false;
    }
    Task parsed;
    parsed.task_id = id->int_value();
    parsed.title = title->string_value();
    if (!parse_priority(priority->string_value(), parsed.priority)) return false;
    if (!parse_task_status(status->string_value(), parsed.status)) return false;
    if (claimed_by->is_string()) {
        parsed.claimed_by = claimed_by->string_value();
    } else if (!claimed_by->is_null()) {
        return false;
    }
    parsed.created_by = created_by->string_value();
    parsed.created_at = created_at->string_value();
    task = parsed;
    return true;
}

std::string format_task_table(const std::vector<Task>& tasks, const std::string& filter) {
    if (tasks.empty()) {
        return filter.empty() ? "No tasks to show.\n"
                              : "No tasks with status " + filter + ".\n";
    }

    std::size_t id_width = 2;  // wide enough for "ID" and for the largest id
    for (const Task& task : tasks) {
        id_width = std::max(id_width, std::to_string(task.task_id).size());
    }
    const std::size_t prefix_width =
        id_width + 2 + kPriorityWidth + 2 + kStatusWidth + 2 + kOwnerWidth + 2;
    const std::size_t title_width = kScreenWidth - prefix_width;

    std::ostringstream out;
    out << pad_left("ID", id_width) << "  " << pad_right("PRIO", kPriorityWidth) << "  "
        << pad_right("STATUS", kStatusWidth) << "  " << pad_right("OWNER", kOwnerWidth) << "  "
        << "TITLE\n";
    out << std::string(id_width, '-') << "  " << std::string(kPriorityWidth, '-') << "  "
        << std::string(kStatusWidth, '-') << "  " << std::string(kOwnerWidth, '-') << "  "
        << std::string(title_width, '-') << "\n";

    for (const Task& task : tasks) {
        const std::vector<std::string> title_lines = wrap_text(task.title, title_width);
        out << pad_left(std::to_string(task.task_id), id_width) << "  "
            << pad_right(to_string(task.priority), kPriorityWidth) << "  "
            << pad_right(to_string(task.status), kStatusWidth) << "  "
            << pad_right(fit_owner(task.claimed_by ? *task.claimed_by : "-"), kOwnerWidth) << "  "
            << title_lines[0] << "\n";
        for (std::size_t i = 1; i < title_lines.size(); ++i) {
            out << std::string(prefix_width, ' ') << title_lines[i] << "\n";
        }
    }
    out << tasks.size() << (tasks.size() == 1 ? " task\n" : " tasks\n");
    return out.str();
}

std::string format_task_detail(const Task& task) {
    std::string out;
    out += wrap_with_indent(task.title, "  Title:      ", "              ");
    out += std::string("  Priority:   ") + to_string(task.priority) + "\n";
    out += std::string("  Status:     ") + to_string(task.status) + "\n";
    out += "  Claimed by: " + (task.claimed_by ? *task.claimed_by : std::string("-")) + "\n";
    out += wrap_with_indent("by " + task.created_by + " at " + task.created_at,
                            "  Created:    ", "              ");
    return out;
}

std::string format_error(const std::string& code, const std::string& message) {
    std::string out = wrap_with_indent("Error [" + code + "]: " + message, "", "  ");
    const std::string hint = hint_for(code);
    if (!hint.empty()) {
        out += wrap_with_indent("Hint: " + hint, "  ", "        ");
    }
    return out;
}

std::string format_local_help() {
    return
        "Commands (type one, then press Enter):\n"
        "  help                   Show this list (works without a server).\n"
        "  help server [COMMAND]  Ask the server for its command list.\n"
        "  add [title]            Add a task; prompts for anything missing.\n"
        "  list [status]          Show tasks; status = open, claimed or done.\n"
        "  claim <id>             Take ownership of an OPEN task.\n"
        "  release <id>           Return a CLAIMED task to OPEN.\n"
        "  done <id>              Mark a task DONE.\n"
        "  delete <id>            Remove a task permanently.\n"
        "  quit                   End the session and exit.\n"
        "  raw <COMMAND> [json]   Send a raw protocol request (for testing).\n";
}

std::string format_server_help(const Json& commands) {
    std::string out = "Server commands:\n";
    for (const Json& entry : commands.items()) {
        if (!entry.is_object()) continue;
        const Json* name = entry.find("name");
        const Json* summary = entry.find("summary");
        const Json* required = entry.find("required_fields");
        if (!name || !name->is_string() || !summary || !summary->is_string()) continue;

        std::string fields;
        if (required != nullptr && required->is_array()) {
            for (const Json& field : required->items()) {
                if (!field.is_string()) continue;
                fields += (fields.empty() ? "" : ", ") + field.string_value();
            }
        }
        out += wrap_with_indent(name->string_value() + ": " + summary->string_value(),
                                "  ", "      ");
        if (!fields.empty()) {
            out += "      requires: " + fields + "\n";
        }
    }
    return out;
}

}  // namespace stcs
