// dispatcher.cpp -- see dispatcher.hpp.
#include "server/dispatcher.hpp"

#include <exception>

#include "server/request_validation.hpp"

namespace stcs {

// ------------------------------------------------------------ small helpers

RequestDispatcher::HandlerResult RequestDispatcher::HandlerResult::success(
    Json payload, std::string log_detail) {
    HandlerResult result;
    result.ok = true;
    result.payload = std::move(payload);
    result.log_detail = std::move(log_detail);
    return result;
}

RequestDispatcher::HandlerResult RequestDispatcher::HandlerResult::failure(
    ProtocolError error, std::string log_detail) {
    HandlerResult result;
    result.ok = false;
    result.error = std::move(error);
    result.log_detail = std::move(log_detail);
    return result;
}

namespace {

// " field=title" when the error names a field, otherwise "".
std::string field_suffix(const ProtocolError& error) {
    const Json* field = error.payload.find("field");
    if (field != nullptr && field->is_string()) {
        return std::string(" field=") + Logger::sanitize(field->string_value(), 24);
    }
    return "";
}

Json task_payload(const Task& task) {
    Json payload = Json::object();
    payload.set("task", task_to_json(task));
    return payload;
}

}  // namespace

std::string RequestDispatcher::serialize(const Json& response) {
    return frame_message(response.dump());
}

ProtocolError RequestDispatcher::not_found_error(long long task_id) {
    ProtocolError error =
        make_error(ErrorCode::NotFound, "No task exists with id " + std::to_string(task_id) + ".");
    error.payload.set("task_id", Json::integer(task_id));
    return error;
}

ProtocolError RequestDispatcher::conflict_error(const char* what, const Task& task) {
    // `what` selects the wording; the payload always reports the CURRENT
    // state so the client can tell the user who owns the task.
    const std::string id = std::to_string(task.task_id);
    std::string message;
    const std::string operation = what;
    if (operation == "claim" && task.status == TaskStatus::Claimed && task.claimed_by) {
        message = "Task " + id + " is already claimed by " + *task.claimed_by + ".";
    } else if (operation == "release") {
        message = "Task " + id + " is not claimed, so it cannot be released.";
    } else if (task.status == TaskStatus::Done) {
        message = "Task " + id + " is already marked done.";
    } else {
        message = "Task " + id + " is " + to_string(task.status) + ".";
    }
    ProtocolError error = make_error(ErrorCode::Conflict, message);
    error.payload.set("task_id", Json::integer(task.task_id));
    error.payload.set("status", Json::text(to_string(task.status)));
    if (task.claimed_by && operation == "claim") {
        error.payload.set("claimed_by", Json::text(*task.claimed_by));
    }
    return error;
}

// ---------------------------------------------------------------- the entry

RequestDispatcher::RequestDispatcher(TaskManager& tasks, SessionRegistry& sessions,
                                     Logger& logger)
    : tasks_(tasks), sessions_(sessions), logger_(logger) {}

void RequestDispatcher::log_frame_error(const std::string& session_id,
                                        const ProtocolError& error,
                                        const std::optional<std::string>& command_text) {
    const std::string command =
        command_text ? " command=\"" + Logger::sanitize(*command_text, 32) + "\"" : "";
    logger_.warn(session_id, std::string("invalid request") + command +
                                 " error=" + to_string(error.code) + field_suffix(error));
}

std::string RequestDispatcher::handle_frame(ClientSession& session, const Frame& frame) {
    switch (frame.status) {
        case FrameStatus::TooLarge: {
            // Resynchronisation already happened inside the framer.
            ProtocolError error = make_error(
                ErrorCode::MessageTooLarge,
                "The message exceeds the 8192-byte limit and was discarded.");
            log_frame_error(session.id(), error);
            return serialize(make_error_response(std::nullopt, std::nullopt, error));
        }
        case FrameStatus::Blank: {
            ProtocolError error =
                make_error(ErrorCode::InvalidMessage, "An empty message was received.");
            log_frame_error(session.id(), error);
            return serialize(make_error_response(std::nullopt, std::nullopt, error));
        }
        case FrameStatus::Message:
            break;
    }
    return handle_message(session, frame.line);
}

std::string RequestDispatcher::handle_message(ClientSession& session, const std::string& line) {
    // 1. Envelope: UTF-8, JSON object, version, type, request_id, command,
    //    payload object, known command.
    Request request;
    EnvelopeFailure failure;
    if (!parse_request_line(line, request, failure)) {
        log_frame_error(session.id(), failure.error, failure.command_text);
        return serialize(
            make_error_response(failure.request_id, failure.command_text, failure.error));
    }

    // 2. Session state (Phase 1, 7.4). INVALID_STATE never closes the socket.
    if (!command_allowed_in_state(request.command, session.state())) {
        std::string message;
        if (request.command == Command::Hello) {
            message = "HELLO has already been completed for this session.";
        } else {
            message = std::string("Command ") + to_string(request.command) +
                      " is not allowed before HELLO. Send HELLO first.";
        }
        ProtocolError error = make_error(ErrorCode::InvalidState, message);
        error.payload.set("state", Json::text(to_string(session.state())));
        logger_.warn(session.id(), std::string(to_string(request.command)) +
                                       " rejected error=INVALID_STATE state=" +
                                       to_string(session.state()));
        return serialize(make_error_response(request.request_id, request.command_text, error));
    }

    // 3. Payload validation and execution. Anything unexpected becomes
    //    SERVER_ERROR: the details go to the log only, never to the client.
    HandlerResult result;
    try {
        result = route(session, request);
    } catch (const std::exception& problem) {
        logger_.error(session.id(), std::string(to_string(request.command)) +
                                        " internal error: " + problem.what());
        result = HandlerResult::failure(
            make_error(ErrorCode::ServerError, "The server could not complete the request."));
    } catch (...) {
        logger_.error(session.id(),
                      std::string(to_string(request.command)) + " internal error (unknown)");
        result = HandlerResult::failure(
            make_error(ErrorCode::ServerError, "The server could not complete the request."));
    }

    // 4. Log the outcome and build the response.
    const std::string detail = result.log_detail.empty() ? "" : " " + result.log_detail;
    if (result.ok) {
        logger_.info(session.id(), std::string(to_string(request.command)) + " success" + detail);
        return serialize(
            make_ok_response(request.request_id, request.command_text, result.payload));
    }
    logger_.warn(session.id(), std::string(to_string(request.command)) +
                                   " rejected error=" + to_string(result.error.code) +
                                   field_suffix(result.error) + detail);
    return serialize(
        make_error_response(request.request_id, request.command_text, result.error));
}

RequestDispatcher::HandlerResult RequestDispatcher::route(ClientSession& session,
                                                          const Request& request) {
    switch (request.command) {
        case Command::Hello:        return on_hello(session, request);
        case Command::Help:         return on_help(request);
        case Command::ListTasks:    return on_list_tasks(request);
        case Command::CreateTask:   return on_create_task(session, request);
        case Command::ClaimTask:    return on_claim_task(session, request);
        case Command::ReleaseTask:  return on_release_task(session, request);
        case Command::CompleteTask: return on_complete_task(request);
        case Command::DeleteTask:   return on_delete_task(request);
        case Command::Quit:         return on_quit(session, request);
    }
    return HandlerResult::failure(make_error(ErrorCode::ServerError, "Unhandled command."));
}

// ----------------------------------------------------------------- handlers

RequestDispatcher::HandlerResult RequestDispatcher::on_hello(ClientSession& session,
                                                             const Request& request) {
    std::string display_name;
    if (auto error = read_display_name(request.payload, display_name)) {
        return HandlerResult::failure(*error);  // session stays CONNECTED: client may retry
    }
    if (!sessions_.reserve_name(session.id(), display_name)) {
        return HandlerResult::failure(make_field_error(
            ErrorCode::NameInUse, "display_name",
            "The display name '" + display_name + "' is already in use."));
    }
    session.mark_ready(display_name);

    Json commands = Json::array();
    for (const CommandInfo& info : command_table()) {
        commands.push_back(Json::text(info.name));
    }
    Json payload = Json::object();
    payload.set("session_id", Json::text(session.id()));
    payload.set("display_name", Json::text(display_name));
    payload.set("server_version", Json::integer(kProtocolVersion));
    payload.set("commands", std::move(commands));
    return HandlerResult::success(std::move(payload), "name=\"" + display_name + "\"");
}

RequestDispatcher::HandlerResult RequestDispatcher::on_help(const Request& request) {
    std::optional<Command> topic;
    if (auto error = read_help_topic(request.payload, topic)) {
        return HandlerResult::failure(*error);
    }
    Json commands = Json::array();
    for (const CommandInfo& info : command_table()) {
        if (topic && info.command != *topic) {
            continue;
        }
        Json entry = Json::object();
        entry.set("name", Json::text(info.name));
        entry.set("summary", Json::text(info.summary));
        Json required = Json::array();
        for (const std::string& field : info.required_fields) {
            required.push_back(Json::text(field));
        }
        entry.set("required_fields", std::move(required));
        commands.push_back(std::move(entry));
    }
    Json payload = Json::object();
    payload.set("commands", std::move(commands));
    return HandlerResult::success(std::move(payload));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_list_tasks(const Request& request) {
    std::optional<TaskStatus> status_filter;
    if (auto error = read_status_filter(request.payload, status_filter)) {
        return HandlerResult::failure(*error);
    }
    const std::vector<Task> tasks = tasks_.list_tasks(status_filter);
    Json list = Json::array();
    for (const Task& task : tasks) {
        list.push_back(task_to_json(task));
    }
    Json payload = Json::object();
    payload.set("tasks", std::move(list));
    payload.set("count", Json::integer(static_cast<long long>(tasks.size())));
    return HandlerResult::success(std::move(payload), "count=" + std::to_string(tasks.size()));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_create_task(ClientSession& session,
                                                                  const Request& request) {
    std::string title;
    Priority priority = Priority::Medium;
    if (auto error = read_title(request.payload, title)) {
        return HandlerResult::failure(*error);
    }
    if (auto error = read_priority(request.payload, priority)) {
        return HandlerResult::failure(*error);
    }
    const Task task = tasks_.create_task(title, priority, session.display_name());
    // The title is private content and is deliberately NOT logged.
    return HandlerResult::success(task_payload(task), "task_id=" + std::to_string(task.task_id));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_claim_task(ClientSession& session,
                                                                 const Request& request) {
    long long task_id = 0;
    if (auto error = read_task_id(request.payload, task_id)) {
        return HandlerResult::failure(*error);
    }
    const TaskResult result = tasks_.claim_task(task_id, session.display_name());
    const std::string detail = "task_id=" + std::to_string(task_id);
    switch (result.outcome) {
        case TaskOutcome::Ok:
            return HandlerResult::success(task_payload(result.task), detail);
        case TaskOutcome::NotFound:
            return HandlerResult::failure(not_found_error(task_id), detail);
        case TaskOutcome::Conflict:
            return HandlerResult::failure(conflict_error("claim", result.task), detail);
    }
    return HandlerResult::failure(make_error(ErrorCode::ServerError, "Unhandled result."));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_release_task(ClientSession& session,
                                                                   const Request& request) {
    long long task_id = 0;
    if (auto error = read_task_id(request.payload, task_id)) {
        return HandlerResult::failure(*error);
    }
    const TaskResult result = tasks_.release_task(task_id);
    const std::string detail = "task_id=" + std::to_string(task_id);
    switch (result.outcome) {
        case TaskOutcome::Ok: {
            Json payload = task_payload(result.task);
            payload.set("released_by", Json::text(session.display_name()));
            return HandlerResult::success(std::move(payload), detail);
        }
        case TaskOutcome::NotFound:
            return HandlerResult::failure(not_found_error(task_id), detail);
        case TaskOutcome::Conflict:
            return HandlerResult::failure(conflict_error("release", result.task), detail);
    }
    return HandlerResult::failure(make_error(ErrorCode::ServerError, "Unhandled result."));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_complete_task(const Request& request) {
    long long task_id = 0;
    if (auto error = read_task_id(request.payload, task_id)) {
        return HandlerResult::failure(*error);
    }
    const TaskResult result = tasks_.complete_task(task_id);
    const std::string detail = "task_id=" + std::to_string(task_id);
    switch (result.outcome) {
        case TaskOutcome::Ok:
            return HandlerResult::success(task_payload(result.task), detail);
        case TaskOutcome::NotFound:
            return HandlerResult::failure(not_found_error(task_id), detail);
        case TaskOutcome::Conflict:
            return HandlerResult::failure(conflict_error("complete", result.task), detail);
    }
    return HandlerResult::failure(make_error(ErrorCode::ServerError, "Unhandled result."));
}

RequestDispatcher::HandlerResult RequestDispatcher::on_delete_task(const Request& request) {
    long long task_id = 0;
    if (auto error = read_task_id(request.payload, task_id)) {
        return HandlerResult::failure(*error);
    }
    const TaskResult result = tasks_.delete_task(task_id);
    const std::string detail = "task_id=" + std::to_string(task_id);
    if (result.outcome == TaskOutcome::NotFound) {
        return HandlerResult::failure(not_found_error(task_id), detail);
    }
    Json payload = Json::object();
    payload.set("task_id", Json::integer(task_id));
    payload.set("deleted", Json::boolean(true));
    return HandlerResult::success(std::move(payload), detail);
}

RequestDispatcher::HandlerResult RequestDispatcher::on_quit(ClientSession& session,
                                                           const Request& request) {
    std::string reason;
    if (auto error = read_quit_reason(request.payload, reason)) {
        return HandlerResult::failure(*error);  // malformed QUIT: session stays open
    }
    session.begin_closing();  // the connection loop closes the socket after the reply
    Json payload = Json::object();
    payload.set("message", Json::text("Session closed."));
    payload.set("session_id", Json::text(session.id()));
    const std::string detail =
        reason.empty() ? "" : "reason=\"" + Logger::sanitize(reason, 60) + "\"";
    return HandlerResult::success(std::move(payload), detail);
}

}  // namespace stcs
