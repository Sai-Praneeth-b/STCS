// client_app.hpp -- the interactive STCS client (user interface + protocol use).
//
// Flow: connect -> HELLO -> command loop -> QUIT. The client keeps NO task
// list: every table or message it prints comes straight from the response to
// the request that was just sent (Phase 1, section 8, "Client-side state").
#pragma once

#include <iosfwd>
#include <string>

#include "client/connection.hpp"
#include "client/input_parser.hpp"
#include "shared/json.hpp"

namespace stcs {

struct ClientConfig {
    std::string host;
    int port = 0;
    std::string display_name;
    bool echo_input = false;  // echo typed lines (used when stdin is not a terminal)
};

class ClientApp {
public:
    ClientApp(ClientConfig config, std::istream& in, std::ostream& out);

    // Runs the whole session. Returns the process exit code:
    //   0 = ended with QUIT (or end of input), 1 = connection/protocol failure.
    int run();

private:
    // What came back from one request/response exchange.
    struct Reply {
        bool ok = false;
        Json payload;
        std::string error_code;
        std::string message;
    };
    enum class Exchange { Reply, ConnectionLost, ProtocolFailure };

    // Setup.
    bool perform_hello();

    // Request/response round trip (protocol use).
    Exchange exchange(const std::string& command, Json payload, Reply& reply);
    void report_exchange_failure(Exchange outcome);

    // Command loop and one handler per user command. Each handler returns
    // false if the connection was lost and the loop must stop.
    int command_loop();
    bool handle_add(const UserCommand& command);
    bool handle_list(const UserCommand& command);
    bool handle_task_command(const UserCommand& command);
    bool handle_help_server(const UserCommand& command);
    bool handle_raw(const UserCommand& command);
    int handle_quit();

    // Display helpers.
    bool show_error_reply(const Reply& reply);
    bool show_task_reply(const Reply& reply, const std::string& headline);
    bool read_input(const std::string& prompt, std::string& line);
    bool prompt_title(std::string& title);
    bool prompt_priority(std::string& priority);
    bool prompt_task_id(const char* verb, long long& task_id);

    ClientConfig config_;
    std::istream& in_;
    std::ostream& out_;
    Connection connection_;
    std::string session_id_;
    std::string display_name_;
    unsigned next_request_number_ = 1;
    std::string failure_detail_;  // explains the last ConnectionLost/ProtocolFailure
};

}  // namespace stcs
