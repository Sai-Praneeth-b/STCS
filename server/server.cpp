// server.cpp -- see server.hpp.
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

// Written by request_shutdown(), read by nothing but the signal handler.
volatile int g_wake_write_fd = -1;

}  // namespace

Server::Server(int port, SessionRegistry& sessions, RequestDispatcher& dispatcher,
               Logger& logger)
    : port_(port), sessions_(sessions), dispatcher_(dispatcher), logger_(logger) {}

void Server::request_shutdown() {
    const int saved_errno = errno;
    const int fd = g_wake_write_fd;
    if (fd >= 0) {
        const char byte = 1;
        ssize_t ignored = ::write(fd, &byte, 1);  // async-signal-safe
        (void)ignored;
    }
    errno = saved_errno;
}

bool Server::start(std::string& error) {
    // The wake-up pipe lets a signal interrupt poll() without any timeout.
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

    // 1. socket(): a TCP (SOCK_STREAM) endpoint over IPv4.
    listener_ = Socket(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener_.valid()) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }

    // Lets the server restart immediately without waiting for TIME_WAIT.
    const int enable = 1;
    if (::setsockopt(listener_.fd(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof enable) != 0) {
        error = std::string("setsockopt(SO_REUSEADDR): ") + std::strerror(errno);
        return false;
    }

    // 2. bind(): attach the socket to 0.0.0.0:<port> (all local interfaces).
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(listener_.fd(), reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        error = "bind to port " + std::to_string(port_) + " failed: " + std::strerror(errno);
        return false;
    }

    // 3. listen(): mark it passive. Clients that connect while a session is
    //    active wait in this backlog until the next accept() (Phase 2 limit).
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

        // 4. accept(): take the next waiting connection off the backlog.
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

        // PHASE 3: start a thread for serve_client() here and continue
        // accepting. Phase 2 serves the client inline: one at a time.
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
        // keep serving until the session ends
    }

    // Cleanup runs on every path: graceful, abrupt, error and shutdown.
    log_session_end(session, reason, detail);
    session.mark_closed();
    sessions_.remove(session.id());
    client.close_now();  // close(): release the descriptor
    logger_.info(session.id(), "session released");
}

bool Server::process_incoming(int client_fd, ClientSession& session, EndReason& reason,
                              std::string& detail) {
    // Wait for data from the client OR a shutdown request, with no timeout.
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

    // 5. recv(): a chunk of the byte stream. It can hold half a message,
    //    one message, or several -- the framer sorts that out.
    char chunk[kReceiveChunkBytes];
    const ssize_t received = ::recv(client_fd, chunk, sizeof chunk, 0);
    if (received == 0) {
        reason = EndReason::PeerClosed;  // orderly shutdown by the peer
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

    // Drain every complete frame: one recv() may have completed several.
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
            reason = EndReason::Graceful;  // QUIT reply has been sent
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

}  // namespace stcs
