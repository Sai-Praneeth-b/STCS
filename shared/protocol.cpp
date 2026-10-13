// protocol.cpp -- see protocol.hpp.
#include "shared/protocol.hpp"

#include "shared/utf8.hpp"

namespace stcs {

// ----------------------------------------------------------------- commands

const std::vector<CommandInfo>& command_table() {
    // Summaries for CREATE_TASK and CLAIM_TASK are the Phase 1 HELP example.
    static const std::vector<CommandInfo> table = {
        {Command::Hello, "HELLO", "Start the session and register a display name.", {"display_name"}},
        {Command::Help, "HELP", "List the commands the server supports.", {}},
        {Command::ListTasks, "LIST_TASKS", "List tasks, optionally filtered by status.", {}},
        {Command::CreateTask, "CREATE_TASK", "Add a task to the shared list.", {"title"}},
        {Command::ClaimTask, "CLAIM_TASK", "Take ownership of an OPEN task.", {"task_id"}},
        {Command::ReleaseTask, "RELEASE_TASK", "Return a CLAIMED task to OPEN.", {"task_id"}},
        {Command::CompleteTask, "COMPLETE_TASK", "Mark a task as DONE.", {"task_id"}},
        {Command::DeleteTask, "DELETE_TASK", "Remove a task permanently.", {"task_id"}},
        {Command::Quit, "QUIT", "End the session gracefully.", {}},
    };
    return table;
}

const CommandInfo& command_info(Command command) {
    for (const CommandInfo& info : command_table()) {
        if (info.command == command) {
            return info;
        }
    }
    return command_table().front();  // unreachable: the table is exhaustive
}

const char* to_string(Command command) {
    return command_info(command).name;
}

bool parse_command(const std::string& text, Command& out) {
    for (const CommandInfo& info : command_table()) {
        if (text == info.name) {
            out = info.command;
            return true;
        }
    }
    return false;
}

// -------------------------------------------------------------- error codes

const char* to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidMessage:   return "INVALID_MESSAGE";
        case ErrorCode::InvalidVersion:   return "INVALID_VERSION";
        case ErrorCode::UnknownCommand:   return "UNKNOWN_COMMAND";
        case ErrorCode::MissingField:     return "MISSING_FIELD";
        case ErrorCode::InvalidField:     return "INVALID_FIELD";
        case ErrorCode::NotFound:         return "NOT_FOUND";
        case ErrorCode::Conflict:         return "CONFLICT";
        case ErrorCode::InvalidState:     return "INVALID_STATE";
        case ErrorCode::NameInUse:        return "NAME_IN_USE";
        case ErrorCode::MessageTooLarge:  return "MESSAGE_TOO_LARGE";
        case ErrorCode::SessionError:     return "SESSION_ERROR";
        case ErrorCode::ServerError:      return "SERVER_ERROR";
    }
    return "SERVER_ERROR";
}

ProtocolError make_error(ErrorCode code, std::string message) {
    ProtocolError error;
    error.code = code;
    error.message = std::move(message);
    return error;
}

ProtocolError make_field_error(ErrorCode code, const std::string& field, std::string message) {
    ProtocolError error = make_error(code, std::move(message));
    error.payload.set("field", Json::text(field));
    return error;
}

// ----------------------------------------------------------- session states

const char* to_string(SessionState state) {
    switch (state) {
        case SessionState::Disconnected: return "DISCONNECTED";
        case SessionState::Connected:    return "CONNECTED";
        case SessionState::Ready:        return "READY";
        case SessionState::Closing:      return "CLOSING";
        case SessionState::Closed:       return "CLOSED";
    }
    return "CLOSED";
}

bool command_allowed_in_state(Command command, SessionState state) {
    switch (state) {
        case SessionState::Connected:
            return command == Command::Hello || command == Command::Help ||
                   command == Command::Quit;
        case SessionState::Ready:
            return command != Command::Hello;
        case SessionState::Disconnected:
        case SessionState::Closing:
        case SessionState::Closed:
            return false;
    }
    return false;
}

// -------------------------------------------------------------- task entity

const char* to_string(Priority priority) {
    switch (priority) {
        case Priority::Low:    return "LOW";
        case Priority::Medium: return "MEDIUM";
        case Priority::High:   return "HIGH";
    }
    return "MEDIUM";
}

const char* to_string(TaskStatus status) {
    switch (status) {
        case TaskStatus::Open:    return "OPEN";
        case TaskStatus::Claimed: return "CLAIMED";
        case TaskStatus::Done:    return "DONE";
    }
    return "OPEN";
}

bool parse_priority(const std::string& text, Priority& out) {
    if (text == "LOW")    { out = Priority::Low;    return true; }
    if (text == "MEDIUM") { out = Priority::Medium; return true; }
    if (text == "HIGH")   { out = Priority::High;   return true; }
    return false;
}

bool parse_task_status(const std::string& text, TaskStatus& out) {
    if (text == "OPEN")    { out = TaskStatus::Open;    return true; }
    if (text == "CLAIMED") { out = TaskStatus::Claimed; return true; }
    if (text == "DONE")    { out = TaskStatus::Done;    return true; }
    return false;
}

Json task_to_json(const Task& task) {
    Json json = Json::object();
    json.set("task_id", Json::integer(task.task_id));
    json.set("title", Json::text(task.title));
    json.set("priority", Json::text(to_string(task.priority)));
    json.set("status", Json::text(to_string(task.status)));
    json.set("claimed_by", task.claimed_by ? Json::text(*task.claimed_by) : Json());
    json.set("created_by", Json::text(task.created_by));
    json.set("created_at", Json::text(task.created_at));
    return json;
}

// --------------------------------------------------------- request envelope

namespace {

// Echoing a recovered value is only useful if it is small and sane.
std::optional<std::string> recover_string(const Json& object, const char* key) {
    const Json* value = object.find(key);
    if (value != nullptr && value->is_string() && !value->string_value().empty() &&
        value->string_value().size() <= kMaxRequestIdBytes) {
        return value->string_value();
    }
    return std::nullopt;
}

bool fail_envelope(EnvelopeFailure& failure, const Json& object, ProtocolError error) {
    failure.error = std::move(error);
    failure.request_id = recover_string(object, "request_id");
    failure.command_text = recover_string(object, "command");
    return false;
}

}  // namespace

bool parse_request_line(const std::string& line, Request& request, EnvelopeFailure& failure) {
    failure = EnvelopeFailure{};

    if (!is_valid_utf8(line)) {
        failure.error = make_error(ErrorCode::InvalidMessage, "The message is not valid UTF-8.");
        return false;
    }
    Json root;
    std::string parse_error;
    if (!Json::parse(line, root, parse_error)) {
        failure.error = make_error(ErrorCode::InvalidMessage,
                                   "The message is not valid JSON (" + parse_error + ").");
        return false;
    }
    if (!root.is_object()) {
        failure.error = make_error(ErrorCode::InvalidMessage,
                                   "The message must be a JSON object.");
        return false;
    }

    // version
    const Json* version = root.find("version");
    if (version == nullptr) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::MissingField, "version",
                                              "Field 'version' is required."));
    }
    if (!version->is_int()) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::InvalidField, "version",
                                              "Field 'version' must be an integer."));
    }
    if (version->int_value() != kProtocolVersion) {
        ProtocolError error = make_field_error(
            ErrorCode::InvalidVersion, "version",
            "Protocol version " + std::to_string(version->int_value()) +
                " is not supported. Supported versions: 1.");
        Json supported = Json::array();
        supported.push_back(Json::integer(kProtocolVersion));
        error.payload.set("supported_versions", std::move(supported));
        return fail_envelope(failure, root, std::move(error));
    }

    // type
    const Json* type = root.find("type");
    if (type == nullptr) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::MissingField, "type",
                                              "Field 'type' is required."));
    }
    if (!type->is_string() || type->string_value() != "REQUEST") {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::InvalidField, "type",
                                              "Field 'type' must be \"REQUEST\"."));
    }

    // request_id
    const Json* request_id = root.find("request_id");
    if (request_id == nullptr) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::MissingField, "request_id",
                                              "Field 'request_id' is required."));
    }
    if (!request_id->is_string() || request_id->string_value().empty() ||
        request_id->string_value().size() > kMaxRequestIdBytes) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::InvalidField, "request_id",
                                              "Field 'request_id' must be a string of 1-64 bytes."));
    }

    // command
    const Json* command = root.find("command");
    if (command == nullptr) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::MissingField, "command",
                                              "Field 'command' is required."));
    }
    if (!command->is_string()) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::InvalidField, "command",
                                              "Field 'command' must be a string."));
    }

    // Unknown command names are an envelope-level failure: the request is
    // well-formed but names something the protocol does not define.
    Command parsed_command = Command::Hello;
    if (!parse_command(command->string_value(), parsed_command)) {
        std::string shown = utf8_truncate(command->string_value(), 40);
        ProtocolError error = make_error(
            ErrorCode::UnknownCommand,
            "Unknown command '" + shown + "'. Send HELP to list the supported commands.");
        error.payload.set("field", Json::text("command"));
        return fail_envelope(failure, root, std::move(error));
    }

    // payload
    const Json* payload = root.find("payload");
    if (payload == nullptr) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::MissingField, "payload",
                                              "Field 'payload' is required (use {} for none)."));
    }
    if (!payload->is_object()) {
        return fail_envelope(failure, root,
                             make_field_error(ErrorCode::InvalidField, "payload",
                                              "Field 'payload' must be a JSON object."));
    }

    request.request_id = request_id->string_value();
    request.command_text = command->string_value();
    request.command = parsed_command;
    request.payload = *payload;
    return true;
}

// ------------------------------------------------------------------ builders

namespace {

Json response_skeleton(const std::optional<std::string>& request_id,
                       const std::optional<std::string>& command,
                       const char* status) {
    Json response = Json::object();
    response.set("version", Json::integer(kProtocolVersion));
    response.set("type", Json::text("RESPONSE"));
    response.set("request_id", request_id ? Json::text(*request_id) : Json());
    response.set("command", command ? Json::text(*command) : Json());
    response.set("status", Json::text(status));
    return response;
}

}  // namespace

Json make_ok_response(const std::optional<std::string>& request_id,
                      const std::optional<std::string>& command,
                      Json payload) {
    Json response = response_skeleton(request_id, command, "OK");
    response.set("payload", std::move(payload));
    return response;
}

Json make_error_response(const std::optional<std::string>& request_id,
                         const std::optional<std::string>& command,
                         const ProtocolError& error) {
    Json response = response_skeleton(request_id, command, "ERROR");
    response.set("error_code", Json::text(to_string(error.code)));
    response.set("message", Json::text(error.message));
    response.set("payload", error.payload);
    return response;
}

Json make_request(const std::string& request_id, const std::string& command, Json payload) {
    Json request = Json::object();
    request.set("version", Json::integer(kProtocolVersion));
    request.set("type", Json::text("REQUEST"));
    request.set("request_id", Json::text(request_id));
    request.set("command", Json::text(command));
    request.set("payload", std::move(payload));
    return request;
}

}  // namespace stcs
