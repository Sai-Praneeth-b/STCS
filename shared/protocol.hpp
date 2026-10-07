// protocol.hpp -- STCS v1 protocol definitions shared by client and server.
//
// This header is the single place where the Phase 1 contract is written down
// in code: limits, command names, error codes, session states, the Task
// entity and the request/response envelope. The client and the server both
// include it, so a constant can never drift between the two programs.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "shared/json.hpp"

namespace stcs {

// ------------------------------------------------------------------- limits

constexpr int kProtocolVersion = 1;

// Phase 1, 7.2: maximum request line (bytes, excluding the '\n'), and the
// size of one recv() call.
constexpr std::size_t kMaxMessageBytes = 8192;
constexpr std::size_t kReceiveChunkBytes = 4096;

// The 8192-byte rule bounds what a client may SEND (server receive buffer,
// NFR-11/NFR-14). Responses are produced by the trusted server and a full
// LIST_TASKS reply can legitimately exceed 8192 bytes (about 50 tasks), so
// the client accepts response lines up to this sanity limit. See the
// design-change summary (clarification C-1) in the Phase 2 report.
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

constexpr std::size_t kMaxRequestIdBytes = 64;

// ---------------------------------------------------------------- commands

enum class Command {
    Hello,
    Help,
    ListTasks,
    CreateTask,
    ClaimTask,
    ReleaseTask,
    CompleteTask,
    DeleteTask,
    Quit,
};

struct CommandInfo {
    Command command;
    const char* name;
    const char* summary;
    std::vector<std::string> required_fields;
};

// All nine Phase 1 commands, in the order used by the HELLO response.
const std::vector<CommandInfo>& command_table();
const CommandInfo& command_info(Command command);
const char* to_string(Command command);
bool parse_command(const std::string& text, Command& out);

// ------------------------------------------------------------- error codes

enum class ErrorCode {
    InvalidMessage,
    InvalidVersion,
    UnknownCommand,
    MissingField,
    InvalidField,
    NotFound,
    Conflict,
    InvalidState,
    NameInUse,
    MessageTooLarge,
    SessionError,
    ServerError,
};

const char* to_string(ErrorCode code);

// An error ready to be turned into an ERROR response. `payload` is always a
// JSON object (often {"field":"title"} or the conflicting task state).
struct ProtocolError {
    ErrorCode code = ErrorCode::ServerError;
    std::string message;
    Json payload = Json::object();
};

ProtocolError make_error(ErrorCode code, std::string message);
ProtocolError make_field_error(ErrorCode code, const std::string& field, std::string message);

// ---------------------------------------------------------- session states

enum class SessionState { Disconnected, Connected, Ready, Closing, Closed };

const char* to_string(SessionState state);

// Phase 1, 7.4: CONNECTED allows HELLO/HELP/QUIT, READY allows everything
// except a second HELLO, the other states allow nothing.
bool command_allowed_in_state(Command command, SessionState state);

// -------------------------------------------------------------- task entity

enum class Priority { Low, Medium, High };
enum class TaskStatus { Open, Claimed, Done };

const char* to_string(Priority priority);
const char* to_string(TaskStatus status);
bool parse_priority(const std::string& text, Priority& out);   // exact "LOW"...
bool parse_task_status(const std::string& text, TaskStatus& out);

struct Task {
    long long task_id = 0;
    std::string title;
    Priority priority = Priority::Medium;
    TaskStatus status = TaskStatus::Open;
    std::optional<std::string> claimed_by;  // null until claimed
    std::string created_by;
    std::string created_at;  // ISO-8601 UTC
};

Json task_to_json(const Task& task);

// ------------------------------------------------------- request / response

struct Request {
    std::string request_id;
    std::string command_text;  // as received
    Command command = Command::Hello;
    Json payload = Json::object();
};

// What the server could recover from a bad request, so the ERROR response can
// still echo request_id / command (Phase 1, 7.3: null only if unrecoverable).
struct EnvelopeFailure {
    ProtocolError error;
    std::optional<std::string> request_id;
    std::optional<std::string> command_text;
};

// Validates the envelope of one request line: UTF-8, JSON object, version,
// type, request_id, command, payload. Does not look inside the payload.
bool parse_request_line(const std::string& line, Request& request, EnvelopeFailure& failure);

// Builds the response envelope. `request_id` / `command` are echoed from the
// request, or null when they could not be recovered.
Json make_ok_response(const std::optional<std::string>& request_id,
                      const std::optional<std::string>& command,
                      Json payload);
Json make_error_response(const std::optional<std::string>& request_id,
                         const std::optional<std::string>& command,
                         const ProtocolError& error);

// Builds a request envelope (used by the client and the tests).
Json make_request(const std::string& request_id, const std::string& command, Json payload);

}  // namespace stcs
