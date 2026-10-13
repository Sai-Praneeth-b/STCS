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

    bool start(std::string& error);

    void run();

    static void request_shutdown();

private:
    enum class EndReason {
        Graceful,
        PeerClosed,
        ReceiveError,
        SendError,
        ServerShutdown,
    };

    void serve_client(Socket client, const sockaddr_in& peer);

    bool process_incoming(int client_fd, ClientSession& session, EndReason& reason,
                          std::string& detail);

    void log_session_end(const ClientSession& session, EndReason reason,
                         const std::string& detail);

    int port_;
    SessionRegistry& sessions_;
    RequestDispatcher& dispatcher_;
    Logger& logger_;
    Socket listener_;
    Socket wake_read_;
    Socket wake_write_;
};

}
