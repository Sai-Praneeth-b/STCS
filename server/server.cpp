#include "server/server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include "shared/protocol.hpp"

namespace stcs {

namespace {

volatile int g_wake_write_fd = -1;

}

Server::Server(int port, SessionRegistry& sessions, RequestDispatcher& dispatcher,
               Logger& logger)
    : port_(port), sessions_(sessions), dispatcher_(dispatcher), logger_(logger) {}

void Server::request_shutdown() {
    const int saved_errno = errno;
    const int fd = g_wake_write_fd;
    if (fd >= 0) {
        const char byte = 1;
        ssize_t ignored = ::write(fd, &byte, 1);
        (void)ignored;
    }
    errno = saved_errno;
}

bool Server::start(std::string& error) {
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    wake_read_ = Socket(pipe_fds[0]);
    wake_write_ = Socket(pipe_fds[1]);
    const int flags = ::fcntl(wake_write_.fd(), F_GETFL, 0);
    ::fcntl(wake_write_.fd(), F_SETFL, flags | O_NONBLOCK);
    g_wake_write_fd = wake_write_.fd();

    listener_ = Socket(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener_.valid()) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }

    const int enable = 1;
    if (::setsockopt(listener_.fd(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable) != 0) {
        error = std::string("setsockopt(SO_REUSEADDR): ") + std::strerror(errno);
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(listener_.fd(), reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        error = "bind to port " + std::to_string(port_) + " failed: " + std::strerror(errno);
        return false;
    }

    if (::listen(listener_.fd(), 5) != 0) {
        error = std::string("listen: ") + std::strerror(errno);
        return false;
    }

    sockaddr_in bound{};
    socklen_t bound_length = sizeof bound;
    ::getsockname(listener_.fd(), reinterpret_cast<sockaddr*>(&bound), &bound_length);
    char text[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &bound.sin_addr, text, sizeof text);
    logger_.info(std::string("Server listening on ") + text + ":" +
                 std::to_string(ntohs(bound.sin_port)));
    return true;
}

void Server::run() {
    logger_.info("Waiting for a client connection (one active client at a time)");
    while (true) {
        pollfd watched[2];
        watched[0] = {listener_.fd(), POLLIN, 0};
        watched[1] = {wake_read_.fd(), POLLIN, 0};
        if (::poll(watched, 2, -1) < 0) {
            if (errno == EINTR) continue;
            logger_.error(std::string("poll failed: ") + std::strerror(errno));
            break;
        }
        if (watched[1].revents != 0) {
            logger_.info("Shutdown requested; stopping the server");
            break;
        }
        if (watched[0].revents == 0) {
            continue;
        }

        sockaddr_in peer{};
        socklen_t peer_length = sizeof peer;
        const int client_fd =
            ::accept(listener_.fd(), reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (client_fd < 0) {
            if (errno != EINTR && errno != ECONNABORTED) {
                logger_.error(std::string("accept failed: ") + std::strerror(errno));
            }
            continue;
        }

        serve_client(Socket(client_fd), peer);
    }
    listener_.close_now();
    logger_.info("Server shutdown complete");
}

void Server::serve_client(Socket client, const sockaddr_in& peer) {
    ClientSession session(sessions_.next_session_id(), format_peer_address(peer));
    sessions_.add(session.id());
    logger_.info("Client " + session.id() + " connected from " + session.peer_address());

    EndReason reason = EndReason::PeerClosed;
    std::string detail;
    while (process_incoming(client.fd(), session, reason, detail)) {
    }

    log_session_end(session, reason, detail);
    session.mark_closed();
    sessions_.remove(session.id());
    client.close_now();
    logger_.info(session.id(), "session released");
}

bool Server::process_incoming(int client_fd, ClientSession& session, EndReason& reason,
                              std::string& detail) {
    pollfd watched[2];
    watched[0] = {client_fd, POLLIN, 0};
    watched[1] = {wake_read_.fd(), POLLIN, 0};
    if (::poll(watched, 2, -1) < 0) {
        if (errno == EINTR) return true;
        reason = EndReason::ReceiveError;
        detail = std::strerror(errno);
        return false;
    }
    if (watched[1].revents != 0) {
        reason = EndReason::ServerShutdown;
        return false;
    }

    char chunk[kReceiveChunkBytes];
    const ssize_t received = ::recv(client_fd, chunk, sizeof chunk, 0);
    if (received == 0) {
        reason = EndReason::PeerClosed;
        if (session.framer().buffered_bytes() > 0) {
            detail = std::to_string(session.framer().buffered_bytes()) +
                     " bytes of an incomplete message discarded";
        }
        return false;
    }
    if (received < 0) {
        if (errno == EINTR) return true;
        reason = EndReason::ReceiveError;
        detail = std::strerror(errno);
        return false;
    }

    session.framer().feed(chunk, static_cast<std::size_t>(received));

    Frame frame;
    while (session.framer().next_frame(frame)) {
        const std::string response = dispatcher_.handle_frame(session, frame);
        std::string send_error;
        if (!send_all(client_fd, response, send_error)) {
            reason = EndReason::SendError;
            detail = send_error;
            return false;
        }
        if (session.state() == SessionState::Closing) {
            reason = EndReason::Graceful;
            return false;
        }
    }
    return true;
}

void Server::log_session_end(const ClientSession& session, EndReason reason,
                             const std::string& detail) {
    switch (reason) {
        case EndReason::Graceful:
            logger_.info(session.id(), "disconnected gracefully");
            break;
        case EndReason::PeerClosed:
            logger_.warn(session.id(),
                         "disconnected unexpectedly (connection closed without QUIT)" +
                             (detail.empty() ? std::string() : "; " + detail));
            break;
        case EndReason::ReceiveError:
            logger_.warn(session.id(), "disconnected unexpectedly (receive error: " + detail + ")");
            break;
        case EndReason::SendError:
            logger_.warn(session.id(), "connection lost while sending (" + detail + ")");
            break;
        case EndReason::ServerShutdown:
            logger_.info(session.id(), "closed by server shutdown");
            break;
    }
}

}
