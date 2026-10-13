// request_validation.hpp -- payload validation for each STCS command.
//
// The envelope (version/type/request_id/command/payload) is checked by
// parse_request_line() in shared/protocol. This module checks what is INSIDE
// the payload: presence, JSON type, length and range of every field
// (NFR-14). Each function returns std::nullopt on success or the
// ProtocolError to send back. Unknown payload fields are ignored, as Phase 1
// 7.3 allows additions without a version change.
//
// Nothing here touches state: a request that fails validation can never
// change the task list.
#pragma once

#include <optional>
#include <string>

#include "shared/protocol.hpp"

namespace stcs {

// HELLO: required display_name.
std::optional<ProtocolError> read_display_name(const Json& payload, std::string& display_name);

// CREATE_TASK: required title (returned trimmed), optional priority (MEDIUM).
std::optional<ProtocolError> read_title(const Json& payload, std::string& title);
std::optional<ProtocolError> read_priority(const Json& payload, Priority& priority);

// CLAIM/RELEASE/COMPLETE/DELETE: required integer task_id >= 1.
std::optional<ProtocolError> read_task_id(const Json& payload, long long& task_id);

// LIST_TASKS: optional status filter.
std::optional<ProtocolError> read_status_filter(const Json& payload,
                                                std::optional<TaskStatus>& status_filter);

// HELP: optional command name (must be one of the nine commands).
std::optional<ProtocolError> read_help_topic(const Json& payload,
                                             std::optional<Command>& topic);

// QUIT: optional reason string (logged only).
std::optional<ProtocolError> read_quit_reason(const Json& payload, std::string& reason);

}  // namespace stcs
