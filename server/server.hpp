// server.hpp -- TCP listener and the single-client serving loop.
//
// Phase 2 control flow (deliberately sequential):
//
//     accept()  ->  serve_client()  ->  back to accept()
//
// Phase 3 MIGRATION POINT: in Server::run(), replace the direct call
//     serve_client(std::move(client), peer);
// with creating one thread that runs serve_client() and keeps accepting.
// serve_client() already owns everything it needs (socket, framer, session
// state) and talks to the shared pieces only through TaskManager,
// SessionRegistry, RequestDispatcher and Logger, which is exactly where the
// Phase 1 design places task_lock, session_lock and the logger mutex. The
// protocol and the client do not change.
#pragma once

#include <netinet/in.h>

#include <string>

#include "server/dispatcher.hpp"
#include "server/logger.hpp"
#include "server/session.hpp"
#include "shared/net.hpp"

namespace stcs {

class Server {
public:
    Server(int port, SessionRegistry& sessions, RequestDispatcher& dispatcher, Logger& logger);

    // socket() + SO_REUSEADDR + bind() + listen(), plus the wake-up pipe used
    // for clean shutdown. Returns false and fills `error` on failure.
    bool start(std::string& error);

    // Accept loop. Returns after a shutdown request (SIGINT / SIGTERM).
    void run();

    // Asks run() / serve_client() to stop. Async-signal-safe: it only write()s
    // one byte to the wake-up pipe, so there is no flag race and no polling
    // timeout.
    static void request_shutdown();

private:
    enum class EndReason {
        Graceful,        // client sent QUIT and received the reply
        PeerClosed,      // recv() returned 0 without a QUIT
        ReceiveError,    // recv() failed (e.g. connection reset)
        SendError,       // send() failed
        ServerShutdown,  // SIGINT / SIGTERM while the session was active
    };

    // Runs one whole session: recv -> frame -> dispatch -> send, until the
    // session ends. Always releases the session before returning.
    void serve_client(Socket client, const sockaddr_in& peer);

    // Reads available bytes and processes every complete frame in them.
    // Returns true while the session should continue.
    bool process_incoming(int client_fd, ClientSession& session, EndReason& reason,
                          std::string& detail);

    void log_session_end(const ClientSession& session, EndReason reason,
                         const std::string& detail);

    int port_;
    SessionRegistry& sessions_;
    RequestDispatcher& dispatcher_;
    Logger& logger_;
    Socket listener_;
    Socket wake_read_;   // becomes readable when shutdown is requested
    Socket wake_write_;
};

}  // namespace stcs
