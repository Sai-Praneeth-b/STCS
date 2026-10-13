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
    bool echo_input = false;
};

class ClientApp {
public:
    ClientApp(ClientConfig config, std::istream& in, std::ostream& out);

    int run();

private:
    struct Reply {
        bool ok = false;
        Json payload;
        std::string error_code;
        std::string message;
    };
    enum class Exchange { Reply, ConnectionLost, ProtocolFailure };

    bool perform_hello();

    Exchange exchange(const std::string& command, Json payload, Reply& reply);
    void report_exchange_failure(Exchange outcome);

    int command_loop();
    bool handle_add(const UserCommand& command);
    bool handle_list(const UserCommand& command);
    bool handle_task_command(const UserCommand& command);
    bool handle_help_server(const UserCommand& command);
    bool handle_raw(const UserCommand& command);
    int handle_quit();

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
    std::string failure_detail_;
};

}
