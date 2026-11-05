#pragma once

#include <optional>
#include <string>

#include "shared/protocol.hpp"

namespace stcs {

std::optional<ProtocolError> read_display_name(const Json& payload, std::string& display_name);

std::optional<ProtocolError> read_title(const Json& payload, std::string& title);
std::optional<ProtocolError> read_priority(const Json& payload, Priority& priority);

std::optional<ProtocolError> read_task_id(const Json& payload, long long& task_id);

std::optional<ProtocolError> read_status_filter(const Json& payload,
                                                std::optional<TaskStatus>& status_filter);

std::optional<ProtocolError> read_help_topic(const Json& payload,
                                             std::optional<Command>& topic);

std::optional<ProtocolError> read_quit_reason(const Json& payload, std::string& reason);

}
