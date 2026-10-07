// connection.hpp -- the client's TCP connection management.
//
// Owns the socket and the receive-side framer. It knows how to connect (with
// a deadline), send one framed line, and receive one complete line from the
// server's byte stream. It knows nothing about commands or the user
// interface.
#pragma once

#include <string>

#include "shared/framing.hpp"
#include "shared/net.hpp"
#include "shared/protocol.hpp"

namespace stcs {

enum class ReceiveStatus {
    Line,       // `line` holds one complete server message
    Closed,     // the server closed the connection (recv() returned 0)
    Error,      // recv() failed; `error` describes why
    TooLarge,   // the server sent a line above the sanity limit
};

class Connection {
public:
    Connection() : framer_(kMaxResponseBytes) {}

    // Resolves `host` and connects with an overall deadline (NFR-09). On
    // failure `error` holds a plain-language sentence for the user.
    bool connect_to(const std::string& host, int port, int timeout_seconds,
                    std::string& error);

    // Sends one framed message completely (loops over partial sends).
    bool send_line(const std::string& framed_message, std::string& error);

    // Blocks until one complete line has been reassembled from the stream
    // (it may take several recv() calls, or none if an earlier recv() already
    // delivered it).
    ReceiveStatus receive_line(std::string& line, std::string& error);

    bool is_open() const { return socket_.valid(); }

    // Half-closes the sending side so the server sees an orderly shutdown,
    // then closes the descriptor.
    void close_gracefully();

private:
    Socket socket_;
    MessageFramer framer_;
};

}  // namespace stcs
