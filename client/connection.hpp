#pragma once

#include <string>

#include "shared/framing.hpp"
#include "shared/net.hpp"
#include "shared/protocol.hpp"

namespace stcs {

enum class ReceiveStatus {
    Line,
    Closed,
    Error,
    TooLarge,
};

class Connection {
public:
    Connection() : framer_(kMaxResponseBytes) {}

    bool connect_to(const std::string& host, int port, int timeout_seconds,
                    std::string& error);

    bool send_line(const std::string& framed_message, std::string& error);

    ReceiveStatus receive_line(std::string& line, std::string& error);

    bool is_open() const { return socket_.valid(); }

    void close_gracefully();

private:
    Socket socket_;
    MessageFramer framer_;
};

}
