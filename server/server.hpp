#pragma once

#include <netinet/in.h>

#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <thread>

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

    // One worker thread per connected client ("thread per client" model).
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void serve_client(Socket client, const sockaddr_in& peer);

    // Joins workers whose client has left (or all of them when wait_all is true).
    void reap_workers(bool wait_all);

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
    std::list<Worker> workers_;  // touched only by the accept thread
};

}
