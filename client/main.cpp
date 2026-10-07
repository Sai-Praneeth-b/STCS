// main.cpp -- STCS client entry point.
//
//   usage: client <host> <port> <display_name>
//
// The port and display name are validated BEFORE any socket is created.
#include <unistd.h>

#include <iostream>
#include <string>

#include "client/client_app.hpp"
#include "shared/net.hpp"
#include "shared/validation.hpp"

namespace {

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <host> <port> <display_name>\n"
              << "  host          server hostname or IPv4 address\n"
              << "  port          server TCP port (1-65535)\n"
              << "  display_name  1-24 characters: letters, digits, space, _ - .\n"
              << "Example: " << program << " 127.0.0.1 5050 Ana\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 2;
    }

    stcs::ClientConfig config;
    config.host = argv[1];
    if (config.host.empty()) {
        std::cerr << "Error: the host must not be empty.\n";
        print_usage(argv[0]);
        return 2;
    }

    std::string reason;
    if (!stcs::parse_port_text(argv[2], config.port, reason)) {
        std::cerr << "Error: invalid port '" << argv[2] << "': " << reason << ".\n";
        return 2;
    }
    config.display_name = argv[3];
    if (!stcs::validate_display_name(config.display_name, reason)) {
        std::cerr << "Error: invalid display name: it " << reason << ".\n";
        return 2;
    }

    stcs::ignore_sigpipe();
    config.echo_input = !::isatty(STDIN_FILENO);
    stcs::ClientApp app(config, std::cin, std::cout);
    return app.run();
}
