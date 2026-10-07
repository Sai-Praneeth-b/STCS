// request_validation.cpp -- see request_validation.hpp.
#include "server/request_validation.hpp"

#include "shared/utf8.hpp"
#include "shared/validation.hpp"

namespace stcs {

namespace {

constexpr std::size_t kMaxQuitReasonBytes = 200;

ProtocolError missing(const std::string& field) {
    return make_field_error(ErrorCode::MissingField, field,
                            "Field '" + field + "' is required.");
}

ProtocolError invalid(const std::string& field, const std::string& message) {
    return make_field_error(ErrorCode::InvalidField, field, message);
}

}  // namespace

std::optional<ProtocolError> read_display_name(const Json& payload, std::string& display_name) {
    const Json* value = payload.find("display_name");
    if (value == nullptr) {
        return missing("display_name");
    }
    if (!value->is_string()) {
        return invalid("display_name", "Field 'display_name' must be a string.");
    }
    std::string reason;
    if (!validate_display_name(value->string_value(), reason)) {
        return invalid("display_name", "Field 'display_name' " + reason + ".");
    }
    display_name = value->string_value();
    return std::nullopt;
}

std::optional<ProtocolError> read_title(const Json& payload, std::string& title) {
    const Json* value = payload.find("title");
    if (value == nullptr) {
        return missing("title");
    }
    if (!value->is_string()) {
        return invalid("title", "Field 'title' must be a string.");
    }
    std::string normalized;
    std::string reason;
    if (!validate_title(value->string_value(), normalized, reason)) {
        return invalid("title", "Field 'title' " + reason + " (1-120 characters).");
    }
    title = normalized;
    return std::nullopt;
}

std::optional<ProtocolError> read_priority(const Json& payload, Priority& priority) {
    priority = Priority::Medium;  // default when the field is absent
    const Json* value = payload.find("priority");
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_string() || !parse_priority(value->string_value(), priority)) {
        priority = Priority::Medium;
        return invalid("priority", "Field 'priority' must be LOW, MEDIUM, or HIGH.");
    }
    return std::nullopt;
}

std::optional<ProtocolError> read_task_id(const Json& payload, long long& task_id) {
    const Json* value = payload.find("task_id");
    if (value == nullptr) {
        return missing("task_id");
    }
    // Only a JSON integer is accepted: "abc", "7", 7.5, true and null are
    // all rejected, as is anything below 1.
    if (!value->is_int() || value->int_value() < 1) {
        return invalid("task_id", "Field 'task_id' must be an integer greater than or equal to 1.");
    }
    task_id = value->int_value();
    return std::nullopt;
}

std::optional<ProtocolError> read_status_filter(const Json& payload,
                                                std::optional<TaskStatus>& status_filter) {
    status_filter = std::nullopt;
    const Json* value = payload.find("status");
    if (value == nullptr) {
        return std::nullopt;
    }
    TaskStatus parsed = TaskStatus::Open;
    if (!value->is_string() || !parse_task_status(value->string_value(), parsed)) {
        return invalid("status", "Field 'status' must be OPEN, CLAIMED, or DONE.");
    }
    status_filter = parsed;
    return std::nullopt;
}

std::optional<ProtocolError> read_help_topic(const Json& payload,
                                             std::optional<Command>& topic) {
    topic = std::nullopt;
    const Json* value = payload.find("command");
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_string()) {
        return invalid("command", "Field 'command' must be a string.");
    }
    Command parsed = Command::Help;
    if (!parse_command(value->string_value(), parsed)) {
        return invalid("command", "Unknown command name '" +
                                      utf8_truncate(value->string_value(), 40) + "'.");
    }
    topic = parsed;
    return std::nullopt;
}

std::optional<ProtocolError> read_quit_reason(const Json& payload, std::string& reason) {
    reason.clear();
    const Json* value = payload.find("reason");
    if (value == nullptr) {
        return std::nullopt;
    }
    if (!value->is_string() || value->string_value().size() > kMaxQuitReasonBytes) {
        return invalid("reason", "Field 'reason' must be a string of at most 200 bytes.");
    }
    reason = value->string_value();
    return std::nullopt;
}

}  // namespace stcs
