#include "client/connection.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace stcs {

namespace {

int connect_with_timeout(int fd, const sockaddr* address, socklen_t length, int timeout_ms) {
    const int original_flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, original_flags | O_NONBLOCK);

    int result = 0;
    if (::connect(fd, address, length) != 0) {
        if (errno != EINPROGRESS) {
            result = errno;
        } else {
            pollfd waiting{fd, POLLOUT, 0};
            int ready;
            do {
                ready = ::poll(&waiting, 1, timeout_ms);
            } while (ready < 0 && errno == EINTR);
            if (ready == 0) {
                result = ETIMEDOUT;
            } else if (ready < 0) {
                result = errno;
            } else {
                int socket_error = 0;
                socklen_t size = sizeof socket_error;
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size);
                result = socket_error;
            }
        }
    }
    ::fcntl(fd, F_SETFL, original_flags);
    return result;
}

std::string describe_connect_failure(const std::string& host, int port, int error_number,
                                     int timeout_seconds) {
    const std::string target = host + ":" + std::to_string(port);
    switch (error_number) {
        case ECONNREFUSED:
            return "Could not connect to " + target + ": connection refused. Is the server running?";
        case ETIMEDOUT:
            return "Could not connect to " + target + ": no answer after " +
                   std::to_string(timeout_seconds) + " seconds. Check the address and that the server is running.";
        case ENETUNREACH:
        case EHOSTUNREACH:
            return "Could not connect to " + target + ": the host is not reachable.";
        default:
            return "Could not connect to " + target + ": " + std::strerror(error_number) + ".";
    }
}

}

bool Connection::connect_to(const std::string& host, int port, int timeout_seconds,
                            std::string& error) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const int lookup = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
    if (lookup != 0) {
        error = "Could not find host '" + host + "' (" + ::gai_strerror(lookup) +
                "). Check the name or IP address.";
        return false;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    int last_error = ECONNREFUSED;
    bool connected = false;
    for (const addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            last_error = ETIMEDOUT;
            break;
        }
        Socket attempt(::socket(candidate->ai_family, candidate->ai_socktype,
                                candidate->ai_protocol));
        if (!attempt.valid()) {
            last_error = errno;
            continue;
        }
        last_error = connect_with_timeout(attempt.fd(), candidate->ai_addr,
                                          candidate->ai_addrlen, static_cast<int>(remaining.count()));
        if (last_error == 0) {
            socket_ = std::move(attempt);
            connected = true;
            break;
        }
    }
    ::freeaddrinfo(results);

    if (!connected) {
        error = describe_connect_failure(host, port, last_error, timeout_seconds);
        return false;
    }
    return true;
}

bool Connection::send_line(const std::string& framed_message, std::string& error) {
    if (!socket_.valid()) {
        error = "not connected";
        return false;
    }
    return send_all(socket_.fd(), framed_message, error);
}

ReceiveStatus Connection::receive_line(std::string& line, std::string& error) {
    while (true) {
        Frame frame;
        while (framer_.next_frame(frame)) {
            if (frame.status == FrameStatus::Message) {
                line = frame.line;
                return ReceiveStatus::Line;
            }
            if (frame.status == FrameStatus::TooLarge) {
                error = "the server sent a message larger than the client accepts";
                return ReceiveStatus::TooLarge;
            }
        }

        char chunk[kReceiveChunkBytes];
        const ssize_t received = ::recv(socket_.fd(), chunk, sizeof chunk, 0);
        if (received == 0) {
            return ReceiveStatus::Closed;
        }
        if (received < 0) {
            if (errno == EINTR) continue;
            error = std::strerror(errno);
            return ReceiveStatus::Error;
        }
        framer_.feed(chunk, static_cast<std::size_t>(received));
    }
}

void Connection::close_gracefully() {
    if (socket_.valid()) {
        ::shutdown(socket_.fd(), SHUT_WR);
        socket_.close_now();
    }
}

}
