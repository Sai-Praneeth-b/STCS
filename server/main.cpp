// main.cpp -- STCS server entry point.
//
//   usage: server <port> [log_file]
//
// Wires the modules together: Logger, TaskManager (data + application logic),
// SessionRegistry, RequestDispatcher (protocol validation + dispatch) and the
// Server (TCP listener + session loop).
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>

#include "server/dispatcher.hpp"
#include "server/logger.hpp"
#include "server/server.hpp"
#include "server/session.hpp"
#include "server/task_manager.hpp"
#include "shared/net.hpp"
#include "shared/validation.hpp"

namespace {

constexpr const char* kDefaultLogPath = "logs/server.log";  // relative to the working directory

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <port> [log_file]\n"
              << "  port      TCP port to listen on (1-65535)\n"
              << "  log_file  log file path (default: " << kDefaultLogPath << ")\n"
              << "Example: " << program << " 5050\n";
}

void install_signal_handlers() {
    struct sigaction action;
    std::memset(&action, 0, sizeof action);
    action.sa_handler = [](int) { stcs::Server::request_shutdown(); };
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;  // no SA_RESTART: blocked calls return EINTR
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    stcs::ignore_sigpipe();
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 2;
    }

    int port = 0;
    std::string reason;
    if (!stcs::parse_port_text(argv[1], port, reason)) {
        std::cerr << "Error: invalid port '" << argv[1] << "': " << reason << ".\n";
        print_usage(argv[0]);
        return 2;
    }
    const std::string log_path = (argc == 3) ? argv[2] : kDefaultLogPath;

    stcs::Logger logger;
    logger.info("Server starting (STCS protocol v1, Phase 2: one client at a time)");
    std::string log_error;
    if (logger.open_file(log_path, log_error)) {
        logger.info("Logging to " + log_path);
    } else {
        logger.warn("File logging disabled: " + log_error);
    }

    stcs::TaskManager tasks;
    stcs::SessionRegistry sessions;
    stcs::RequestDispatcher dispatcher(tasks, sessions, logger);
    stcs::Server server(port, sessions, dispatcher, logger);

    std::string start_error;
    if (!server.start(start_error)) {
        logger.error("Cannot start server: " + start_error);
        return 1;
    }

    install_signal_handlers();
    server.run();
    return 0;
}
