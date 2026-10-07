// net.cpp -- see net.hpp.
#include "shared/net.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

namespace stcs {

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close_now();
        fd_ = other.release();
    }
    return *this;
}

int Socket::release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

void Socket::close_now() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool send_all(int fd, const char* data, std::size_t length, std::string& error) {
    std::size_t sent_total = 0;
    while (sent_total < length) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const ssize_t sent = ::send(fd, data + sent_total, length - sent_total, flags);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::strerror(errno);
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool send_all(int fd, const std::string& data, std::string& error) {
    return send_all(fd, data.data(), data.size(), error);
}

std::string format_peer_address(const struct sockaddr_in& address) {
    char text[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &address.sin_addr, text, sizeof text);
    return std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
}

void ignore_sigpipe() {
    std::signal(SIGPIPE, SIG_IGN);
}

}  // namespace stcs
