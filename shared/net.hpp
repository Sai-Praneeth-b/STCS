// net.hpp -- thin RAII and error-checking wrappers around POSIX sockets.
//
// The raw calls (socket, bind, listen, accept, connect, send, recv, close)
// stay visible in server/server.cpp and client/connection.cpp; this header
// only holds the pieces that both programs need and that are easy to get
// wrong: closing a descriptor exactly once, and send() writing less than
// requested.
#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <string>

namespace stcs {

// Owns a file descriptor and closes it in the destructor (move-only).
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.release()) {}
    Socket& operator=(Socket&& other) noexcept;
    ~Socket() { close_now(); }

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release();        // gives up ownership without closing
    void close_now();     // closes immediately (safe to call twice)

private:
    int fd_ = -1;
};

// send() may accept fewer bytes than requested. This loops until every byte
// has been handed to the kernel, retrying on EINTR. Returns false and fills
// `error` (strerror text) if the peer is gone or another error occurs.
bool send_all(int fd, const char* data, std::size_t length, std::string& error);
bool send_all(int fd, const std::string& data, std::string& error);

// "a.b.c.d:port" for log lines.
std::string format_peer_address(const sockaddr_in& address);

// Ignores SIGPIPE so that writing to a closed socket returns EPIPE instead of
// killing the process.
void ignore_sigpipe();

}  // namespace stcs
