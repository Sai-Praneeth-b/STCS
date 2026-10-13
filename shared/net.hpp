#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <string>

namespace stcs {

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
    int release();
    void close_now();

private:
    int fd_ = -1;
};

bool send_all(int fd, const char* data, std::size_t length, std::string& error);
bool send_all(int fd, const std::string& data, std::string& error);

std::string format_peer_address(const sockaddr_in& address);

void ignore_sigpipe();

}
