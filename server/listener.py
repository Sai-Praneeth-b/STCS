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

    std::string handle_frame(ClientSession& session, const Frame& frame);

private:
    struct HandlerResult {
        bool ok = false;
        Json payload = Json::object();
        ProtocolError error;
        std::string log_detail;

        static HandlerResult success(Json payload, std::string log_detail = "");
        static HandlerResult failure(ProtocolError error, std::string log_detail = "");
    };

    std::string handle_message(ClientSession& session, const std::string& line);
    HandlerResult route(ClientSession& session, const Request& request);

    HandlerResult on_hello(ClientSession& session, const Request& request);
    HandlerResult on_help(const Request& request);
    HandlerResult on_list_tasks(const Request& request);
    HandlerResult on_create_task(ClientSession& session, const Request& request);
    HandlerResult on_claim_task(ClientSession& session, const Request& request);
    HandlerResult on_release_task(ClientSession& session, const Request& request);
    HandlerResult on_complete_task(const Request& request);
    HandlerResult on_delete_task(const Request& request);
    HandlerResult on_quit(ClientSession& session, const Request& request);

    static ProtocolError conflict_error(const char* what, const Task& task);
    static ProtocolError not_found_error(long long task_id);

    void log_frame_error(const std::string& session_id, const ProtocolError& error,
                         const std::optional<std::string>& command_text = std::nullopt);
    static std::string serialize(const Json& response);

    TaskManager& tasks_;
    SessionRegistry& sessions_;
    Logger& logger_;
};

}
