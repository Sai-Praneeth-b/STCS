// client_app.cpp -- see client_app.hpp.
#include "client/client_app.hpp"

#include <cctype>
#include <iostream>

#include "client/display.hpp"
#include "shared/framing.hpp"
#include "shared/protocol.hpp"
#include "shared/utf8.hpp"
#include "shared/validation.hpp"

namespace stcs {

namespace {

constexpr int kConnectTimeoutSeconds = 10;  // NFR-09
constexpr const char* kPrompt = "stcs> ";

std::string upper_case(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

ClientApp::ClientApp(ClientConfig config, std::istream& in, std::ostream& out)
    : config_(std::move(config)), in_(in), out_(out) {}

// ------------------------------------------------------------------- run

int ClientApp::run() {
    out_ << "Connecting to " << config_.host << ":" << config_.port << " ...\n" << std::flush;
    std::string error;
    if (!connection_.connect_to(config_.host, config_.port, kConnectTimeoutSeconds, error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    out_ << "Connected.\n";

    if (!perform_hello()) {
        connection_.close_gracefully();
        return 1;
    }
    out_ << "Type 'help' for a list of commands.\n";
    const int exit_code = command_loop();
    connection_.close_gracefully();
    return exit_code;
}

// ------------------------------------------------------- request / response

ClientApp::Exchange ClientApp::exchange(const std::string& command, Json payload, Reply& reply) {
    const std::string request_id = "r" + std::to_string(next_request_number_++);
    const std::string framed =
        frame_message(make_request(request_id, command, std::move(payload)).dump());

    std::string error;
    if (!connection_.send_line(framed, error)) {
        failure_detail_ = "The connection to the server was lost (" + error + ").";
        return Exchange::ConnectionLost;
    }

    std::string line;
    switch (connection_.receive_line(line, error)) {
        case ReceiveStatus::Line:
            break;
        case ReceiveStatus::Closed:
            failure_detail_ = "The server closed the connection.";
            return Exchange::ConnectionLost;
        case ReceiveStatus::Error:
            failure_detail_ = "The connection to the server was lost (" + error + ").";
            return Exchange::ConnectionLost;
        case ReceiveStatus::TooLarge:
            failure_detail_ = "The server sent a reply that is too large (" + error + ").";
            return Exchange::ProtocolFailure;
    }

    Json response;
    std::string parse_error;
    if (!is_valid_utf8(line) || !Json::parse(line, response, parse_error) || !response.is_object()) {
        failure_detail_ = "The server sent a reply the client could not understand.";
        return Exchange::ProtocolFailure;
    }
    const Json* type = response.find("type");
    const Json* id = response.find("request_id");
    const Json* status = response.find("status");
    const Json* payload_field = response.find("payload");
    if (!type || !type->is_string() || type->string_value() != "RESPONSE" || !id ||
        !id->is_string() || id->string_value() != request_id || !status || !status->is_string() ||
        !payload_field) {
        // The server echoes every request_id, so a mismatch means the
        // conversation is out of step. Ending the session is the safe choice.
        failure_detail_ = "The server's reply did not match the request that was sent.";
        return Exchange::ProtocolFailure;
    }

    reply = Reply{};
    reply.payload = *payload_field;
    if (status->string_value() == "OK") {
        reply.ok = true;
    } else {
        const Json* code = response.find("error_code");
        const Json* message = response.find("message");
        reply.ok = false;
        reply.error_code = (code && code->is_string()) ? code->string_value() : "SERVER_ERROR";
        reply.message = (message && message->is_string()) ? message->string_value()
                                                          : "The server reported an error.";
    }
    return Exchange::Reply;
}

void ClientApp::report_exchange_failure(Exchange outcome) {
    out_ << wrap_with_indent("Error: " + failure_detail_, "", "  ");
    if (outcome == Exchange::ConnectionLost) {
        out_ << "The session has ended. Start the client again to reconnect.\n";
    } else {
        out_ << "The session cannot continue.\n";
    }
    out_ << std::flush;
}

// ----------------------------------------------------------------- HELLO

bool ClientApp::perform_hello() {
    std::string name = config_.display_name;
    while (true) {
        Json payload = Json::object();
        payload.set("display_name", Json::text(name));
        Reply reply;
        const Exchange outcome = exchange("HELLO", std::move(payload), reply);
        if (outcome != Exchange::Reply) {
            report_exchange_failure(outcome);
            return false;
        }
        if (reply.ok) {
            const Json* session_id = reply.payload.find("session_id");
            const Json* confirmed = reply.payload.find("display_name");
            if (!session_id || !session_id->is_string() || !confirmed || !confirmed->is_string()) {
                failure_detail_ = "The server's HELLO reply was incomplete.";
                report_exchange_failure(Exchange::ProtocolFailure);
                return false;
            }
            session_id_ = session_id->string_value();
            display_name_ = confirmed->string_value();
            out_ << "Session " << session_id_ << " started as '" << display_name_ << "'.\n";
            return true;
        }

        out_ << format_error(reply.error_code, reply.message);
        const bool name_problem = reply.error_code == "NAME_IN_USE" ||
                                  reply.error_code == "INVALID_FIELD" ||
                                  reply.error_code == "MISSING_FIELD";
        if (!name_problem) {
            return false;  // e.g. INVALID_VERSION: retrying cannot help
        }
        // A failed HELLO leaves the session CONNECTED, so ask for another name.
        while (true) {
            std::string candidate;
            if (!read_input("Enter a different display name: ", candidate)) {
                out_ << "\nNo name entered; closing.\n";
                return false;
            }
            candidate = trim_ascii(candidate);
            std::string reason;
            if (validate_display_name(candidate, reason)) {
                name = candidate;
                break;
            }
            out_ << wrap_with_indent("The display name " + reason + ".", "", "  ");
        }
    }
}

// ------------------------------------------------------------- input helpers

bool ClientApp::read_input(const std::string& prompt, std::string& line) {
    out_ << prompt << std::flush;
    if (!std::getline(in_, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (config_.echo_input) {
        out_ << line << "\n";  // input is not a terminal: show what was "typed"
    }
    return true;
}

bool ClientApp::prompt_title(std::string& title) {
    while (true) {
        std::string typed;
        if (!read_input("Title (or /cancel): ", typed)) return false;
        if (trim_ascii(typed) == "/cancel") {
            title.clear();
            return true;
        }
        std::string reason;
        if (validate_title(typed, title, reason)) return true;
        out_ << wrap_with_indent("The title " + reason + " (1-120 characters). Try again.", "", "  ");
    }
}

bool ClientApp::prompt_priority(std::string& priority) {
    while (true) {
        std::string typed;
        if (!read_input("Priority [LOW/MEDIUM/HIGH, Enter = MEDIUM]: ", typed)) return false;
        typed = upper_case(trim_ascii(typed));
        if (typed.empty()) {
            priority = "MEDIUM";
            return true;
        }
        Priority parsed = Priority::Medium;
        if (parse_priority(typed, parsed)) {
            priority = typed;
            return true;
        }
        out_ << "Priority must be LOW, MEDIUM or HIGH. Try again.\n";
    }
}

bool ClientApp::prompt_task_id(const char* verb, long long& task_id) {
    while (true) {
        std::string typed;
        if (!read_input(std::string("Task id to ") + verb + " (or /cancel): ", typed)) return false;
        typed = trim_ascii(typed);
        if (typed == "/cancel") {
            task_id = 0;
            return true;
        }
        std::string reason;
        if (parse_task_id_text(typed, task_id, reason)) return true;
        out_ << wrap_with_indent("The task id " + reason + ". Try again.", "", "  ");
    }
}

// ----------------------------------------------------------- reply display

bool ClientApp::show_error_reply(const Reply& reply) {
    out_ << format_error(reply.error_code, reply.message);
    return true;  // a server-side ERROR never ends the session by itself
}

bool ClientApp::show_task_reply(const Reply& reply, const std::string& headline) {
    const Json* task_json = reply.payload.find("task");
    Task task;
    if (task_json == nullptr || !task_from_json(*task_json, task)) {
        failure_detail_ = "The server's reply did not contain a valid task.";
        report_exchange_failure(Exchange::ProtocolFailure);
        return false;
    }
    out_ << wrap_with_indent(headline.empty() ? "" : headline, "", "  ");
    out_ << format_task_detail(task);
    return true;
}

// ------------------------------------------------------------ command loop

int ClientApp::command_loop() {
    while (true) {
        std::string line;
        if (!read_input(kPrompt, line)) {
            out_ << "\nEnd of input; closing the session.\n";
            return handle_quit();
        }
        const UserCommand command = parse_user_command(line);
        bool alive = true;
        switch (command.kind) {
            case UserCommandKind::Empty:
                out_ << "Enter a command (type 'help' for the list).\n";
                break;
            case UserCommandKind::Invalid:
                out_ << wrap_with_indent(command.error, "", "  ");
                break;
            case UserCommandKind::Help:
                out_ << format_local_help();
                break;
            case UserCommandKind::HelpServer:
                alive = handle_help_server(command);
                break;
            case UserCommandKind::Add:
                alive = handle_add(command);
                break;
            case UserCommandKind::List:
                alive = handle_list(command);
                break;
            case UserCommandKind::Claim:
            case UserCommandKind::Release:
            case UserCommandKind::Done:
            case UserCommandKind::Delete:
                alive = handle_task_command(command);
                break;
            case UserCommandKind::Raw:
                alive = handle_raw(command);
                break;
            case UserCommandKind::Quit:
                return handle_quit();
        }
        if (!alive) {
            return 1;
        }
    }
}

bool ClientApp::handle_add(const UserCommand& command) {
    std::string title;
    if (command.title.empty()) {
        if (!prompt_title(title)) {
            out_ << "\nEnd of input; closing the session.\n";
            handle_quit();
            return true;
        }
    } else {
        std::string reason;
        if (!validate_title(command.title, title, reason)) {
            out_ << wrap_with_indent("The title " + reason + " (1-120 characters).", "", "  ");
            return true;
        }
    }
    if (title.empty()) {
        out_ << "Cancelled.\n";
        return true;
    }
    std::string priority = "MEDIUM";
    if (command.title.empty() && !prompt_priority(priority)) {
        out_ << "\nEnd of input; closing the session.\n";
        handle_quit();
        return true;
    }

    Json payload = Json::object();
    payload.set("title", Json::text(title));
    payload.set("priority", Json::text(priority));
    Reply reply;
    const Exchange outcome = exchange("CREATE_TASK", std::move(payload), reply);
    if (outcome != Exchange::Reply) {
        report_exchange_failure(outcome);
        return false;
    }
    if (!reply.ok) return show_error_reply(reply);
    const Json* task_json = reply.payload.find("task");
    const Json* id = task_json ? task_json->find("task_id") : nullptr;
    const std::string headline =
        "Created task " + (id && id->is_int() ? std::to_string(id->int_value()) : "?") + ".";
    return show_task_reply(reply, headline);
}

bool ClientApp::handle_list(const UserCommand& command) {
    Json payload = Json::object();
    if (command.status) payload.set("status", Json::text(*command.status));
    Reply reply;
    const Exchange outcome = exchange("LIST_TASKS", std::move(payload), reply);
    if (outcome != Exchange::Reply) {
        report_exchange_failure(outcome);
        return false;
    }
    if (!reply.ok) return show_error_reply(reply);

    const Json* list = reply.payload.find("tasks");
    std::vector<Task> tasks;
    bool well_formed = list != nullptr && list->is_array();
    if (well_formed) {
        for (const Json& item : list->items()) {
            Task task;
            if (!task_from_json(item, task)) {
                well_formed = false;
                break;
            }
            tasks.push_back(task);
        }
    }
    if (!well_formed) {
        failure_detail_ = "The server's task list was not in the expected format.";
        report_exchange_failure(Exchange::ProtocolFailure);
        return false;
    }
    out_ << format_task_table(tasks, command.status ? *command.status : "");
    return true;
}

bool ClientApp::handle_task_command(const UserCommand& command) {
    const char* protocol_command = "";
    const char* verb = "";
    switch (command.kind) {
        case UserCommandKind::Claim:   protocol_command = "CLAIM_TASK";    verb = "claim"; break;
        case UserCommandKind::Release: protocol_command = "RELEASE_TASK";  verb = "release"; break;
        case UserCommandKind::Done:    protocol_command = "COMPLETE_TASK"; verb = "complete"; break;
        case UserCommandKind::Delete:  protocol_command = "DELETE_TASK";   verb = "delete"; break;
        default: return true;
    }

    long long task_id = command.task_id.value_or(0);
    if (!command.task_id) {
        if (!prompt_task_id(verb, task_id)) {
            out_ << "\nEnd of input; closing the session.\n";
            handle_quit();
            return true;
        }
        if (task_id == 0) {
            out_ << "Cancelled.\n";
            return true;
        }
    }

    Json payload = Json::object();
    payload.set("task_id", Json::integer(task_id));
    Reply reply;
    const Exchange outcome = exchange(protocol_command, std::move(payload), reply);
    if (outcome != Exchange::Reply) {
        report_exchange_failure(outcome);
        return false;
    }
    if (!reply.ok) return show_error_reply(reply);

    const std::string id_text = std::to_string(task_id);
    switch (command.kind) {
        case UserCommandKind::Claim:
            return show_task_reply(reply, "Claimed task " + id_text + ".");
        case UserCommandKind::Release: {
            const Json* by = reply.payload.find("released_by");
            const std::string who = (by && by->is_string()) ? " (released by " + by->string_value() + ")" : "";
            return show_task_reply(reply, "Released task " + id_text + who + ".");
        }
        case UserCommandKind::Done:
            return show_task_reply(reply, "Marked task " + id_text + " as done.");
        case UserCommandKind::Delete:
            out_ << "Deleted task " << id_text << ".\n";
            return true;
        default:
            return true;
    }
}

bool ClientApp::handle_help_server(const UserCommand& command) {
    Json payload = Json::object();
    if (command.help_topic) payload.set("command", Json::text(*command.help_topic));
    Reply reply;
    const Exchange outcome = exchange("HELP", std::move(payload), reply);
    if (outcome != Exchange::Reply) {
        report_exchange_failure(outcome);
        return false;
    }
    if (!reply.ok) return show_error_reply(reply);
    const Json* commands = reply.payload.find("commands");
    if (commands == nullptr || !commands->is_array()) {
        failure_detail_ = "The server's HELP reply was not in the expected format.";
        report_exchange_failure(Exchange::ProtocolFailure);
        return false;
    }
    out_ << format_server_help(*commands);
    return true;
}

bool ClientApp::handle_raw(const UserCommand& command) {
    // Developer aid: send any command name (even an unknown one) with a JSON
    // object payload, to show how the server answers invalid requests.
    Json payload = Json::object();
    if (!command.raw_payload_text.empty()) {
        std::string parse_error;
        if (!Json::parse(command.raw_payload_text, payload, parse_error) || !payload.is_object()) {
            out_ << "The payload must be a JSON object, e.g. {\"task_id\":1}.\n";
            return true;
        }
    }
    Reply reply;
    const Exchange outcome = exchange(command.raw_command, std::move(payload), reply);
    if (outcome != Exchange::Reply) {
        report_exchange_failure(outcome);
        return false;
    }
    if (!reply.ok) {
        return show_error_reply(reply);
    }
    out_ << wrap_with_indent("Server replied OK: " + reply.payload.dump(), "", "  ");
    return true;
}

int ClientApp::handle_quit() {
    Reply reply;
    const Exchange outcome = exchange("QUIT", Json::object(), reply);
    if (outcome == Exchange::Reply && reply.ok) {
        out_ << "Session closed. Goodbye.\n";
        return 0;
    }
    if (outcome == Exchange::Reply) {
        show_error_reply(reply);
        out_ << "Closing the connection anyway. Goodbye.\n";
        return 0;
    }
    report_exchange_failure(outcome);
    return 1;
}

}  // namespace stcs
