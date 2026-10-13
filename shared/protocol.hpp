#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "shared/json.hpp"

namespace stcs {


constexpr int kProtocolVersion = 1;

constexpr std::size_t kMaxMessageBytes = 8192;
constexpr std::size_t kReceiveChunkBytes = 4096;

constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

constexpr std::size_t kMaxRequestIdBytes = 64;


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

const std::vector<CommandInfo>& command_table();
const CommandInfo& command_info(Command command);
const char* to_string(Command command);
bool parse_command(const std::string& text, Command& out);


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

struct ProtocolError {
    ErrorCode code = ErrorCode::ServerError;
    std::string message;
    Json payload = Json::object();
};

ProtocolError make_error(ErrorCode code, std::string message);
ProtocolError make_field_error(ErrorCode code, const std::string& field, std::string message);


enum class SessionState { Disconnected, Connected, Ready, Closing, Closed };

const char* to_string(SessionState state);

bool command_allowed_in_state(Command command, SessionState state);


enum class Priority { Low, Medium, High };
enum class TaskStatus { Open, Claimed, Done };

const char* to_string(Priority priority);
const char* to_string(TaskStatus status);
bool parse_priority(const std::string& text, Priority& out);
bool parse_task_status(const std::string& text, TaskStatus& out);

struct Task {
    long long task_id = 0;
    std::string title;
    Priority priority = Priority::Medium;
    TaskStatus status = TaskStatus::Open;
    std::optional<std::string> claimed_by;
    std::string created_by;
    std::string created_at;
};

Json task_to_json(const Task& task);


struct Request {
    std::string request_id;
    std::string command_text;
    Command command = Command::Hello;
    Json payload = Json::object();
};

struct EnvelopeFailure {
    ProtocolError error;
    std::optional<std::string> request_id;
    std::optional<std::string> command_text;
};

bool parse_request_line(const std::string& line, Request& request, EnvelopeFailure& failure);

Json make_ok_response(const std::optional<std::string>& request_id,
                      const std::optional<std::string>& command,
                      Json payload);
Json make_error_response(const std::optional<std::string>& request_id,
                         const std::optional<std::string>& command,
                         const ProtocolError& error);

Json make_request(const std::string& request_id, const std::string& command, Json payload);

}
