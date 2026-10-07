// dispatcher.hpp -- turns one received frame into one response.
//
// This is the "protocol validation + command dispatch" layer of the server:
//
//   Frame --> envelope check --> unknown command? --> session-state check
//         --> payload validation --> task manager --> response + log line
//
// It has no socket code: it takes a Frame (already cut out of the TCP byte
// stream by the framer) and returns the bytes to send back. That makes it
// testable in-process and lets Phase 3 call the very same function from one
// thread per client.
#pragma once

#include <string>

#include "server/logger.hpp"
#include "server/session.hpp"
#include "server/task_manager.hpp"
#include "shared/framing.hpp"

namespace stcs {

class RequestDispatcher {
public:
    RequestDispatcher(TaskManager& tasks, SessionRegistry& sessions, Logger& logger);

    // Processes one frame for `session` and returns the framed response
    // (JSON + '\n'). Never throws and never returns an empty string: every
    // frame, valid or not, gets exactly one response (Phase 1, 7.3).
    // After a successful QUIT, session.state() is CLOSING.
    std::string handle_frame(ClientSession& session, const Frame& frame);

private:
    // Result of one command handler.
    struct HandlerResult {
        bool ok = false;
        Json payload = Json::object();  // success payload
        ProtocolError error;            // failure details
        std::string log_detail;         // e.g. "task_id=3", added to the log line

        static HandlerResult success(Json payload, std::string log_detail = "");
        static HandlerResult failure(ProtocolError error, std::string log_detail = "");
    };

    std::string handle_message(ClientSession& session, const std::string& line);
    HandlerResult route(ClientSession& session, const Request& request);

    // One handler per command (the "application logic" entry points).
    HandlerResult on_hello(ClientSession& session, const Request& request);
    HandlerResult on_help(const Request& request);
    HandlerResult on_list_tasks(const Request& request);
    HandlerResult on_create_task(ClientSession& session, const Request& request);
    HandlerResult on_claim_task(ClientSession& session, const Request& request);
    HandlerResult on_release_task(ClientSession& session, const Request& request);
    HandlerResult on_complete_task(const Request& request);
    HandlerResult on_delete_task(const Request& request);
    HandlerResult on_quit(ClientSession& session, const Request& request);

    // Builds the ERROR for a CONFLICT / NOT_FOUND task result.
    static ProtocolError conflict_error(const char* what, const Task& task);
    static ProtocolError not_found_error(long long task_id);

    void log_frame_error(const std::string& session_id, const ProtocolError& error,
                         const std::optional<std::string>& command_text = std::nullopt);
    static std::string serialize(const Json& response);

    TaskManager& tasks_;
    SessionRegistry& sessions_;
    Logger& logger_;
};

}  // namespace stcs
