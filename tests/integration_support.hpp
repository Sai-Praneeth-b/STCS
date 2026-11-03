#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "shared/framing.hpp"
#include "shared/json.hpp"
#include "shared/net.hpp"
#include "shared/protocol.hpp"
#include "tests/test_support.hpp"

using namespace stcs;

namespace {

std::string g_bin_dir = "bin";
constexpr int kWatchdogMs = 15000;


class Process {
public:
    Process(const std::vector<std::string>& argv, bool pipe_stdin) {
        int out_pipe[2];
        int in_pipe[2] = {-1, -1};
        if (::pipe(out_pipe) != 0) throw std::runtime_error("pipe failed");
        if (pipe_stdin && ::pipe(in_pipe) != 0) throw std::runtime_error("pipe failed");
        pid_ = ::fork();
        if (pid_ < 0) throw std::runtime_error("fork failed");
        if (pid_ == 0) {
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::dup2(out_pipe[1], STDERR_FILENO);
            if (pipe_stdin) ::dup2(in_pipe[0], STDIN_FILENO);
            else { int null = ::open("/dev/null", 0); ::dup2(null, STDIN_FILENO); }
            ::close(out_pipe[0]); ::close(out_pipe[1]);
            if (pipe_stdin) { ::close(in_pipe[0]); ::close(in_pipe[1]); }
            std::vector<char*> args;
            for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
            args.push_back(nullptr);
            ::execv(args[0], args.data());
            _exit(127);
        }
        ::close(out_pipe[1]);
        out_fd_ = out_pipe[0];
        if (pipe_stdin) { ::close(in_pipe[0]); in_fd_ = in_pipe[1]; }
    }
    ~Process() {
        if (in_fd_ >= 0) ::close(in_fd_);
        if (out_fd_ >= 0) ::close(out_fd_);
        if (!reaped_ && pid_ > 0) { ::kill(pid_, SIGKILL); ::waitpid(pid_, nullptr, 0); }
    }

    void write_stdin(const std::string& text) {
        std::size_t done = 0;
        while (done < text.size()) {
            const ssize_t n = ::write(in_fd_, text.data() + done, text.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("write to child failed");
            done += static_cast<std::size_t>(n);
        }
    }
    void close_stdin() { if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; } }

    bool read_until(const std::string& marker, std::size_t from = 0) {
        while (captured_.find(marker, from) == std::string::npos) {
            if (!read_more()) return false;
        }
        return true;
    }
    void read_all() { while (read_more()) {} }
    // Keeps the child's stdout pipe from filling up (a full pipe blocks the child's logging).
    void drain_for(int milliseconds) {
        pollfd p{out_fd_, POLLIN, 0};
        if (::poll(&p, 1, milliseconds) > 0) {
            char buffer[4096];
            const ssize_t n = ::read(out_fd_, buffer, sizeof buffer);
            if (n > 0) captured_.append(buffer, static_cast<std::size_t>(n));
        }
    }
    const std::string& output() const { return captured_; }

    void send_signal(int sig) { ::kill(pid_, sig); }
    int wait_status() {
        int status = 0;
        ::waitpid(pid_, &status, 0);
        reaped_ = true;
        return status;
    }
    int exit_code() {
        const int status = wait_status();
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1000 + WTERMSIG(status);
    }

private:
    bool read_more() {
        pollfd p{out_fd_, POLLIN, 0};
        if (::poll(&p, 1, kWatchdogMs) <= 0) throw std::runtime_error("watchdog: no output from child process");
        char buffer[4096];
        const ssize_t n = ::read(out_fd_, buffer, sizeof buffer);
        if (n <= 0) return false;
        captured_.append(buffer, static_cast<std::size_t>(n));
        return true;
    }
    pid_t pid_ = -1;
    int out_fd_ = -1;
    int in_fd_ = -1;
    bool reaped_ = false;
    std::string captured_;
};

inline int find_free_port() {
    Socket probe(::socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(probe.fd(), reinterpret_cast<sockaddr*>(&address), sizeof address) != 0)
        throw std::runtime_error("cannot find a free port");
    socklen_t length = sizeof address;
    ::getsockname(probe.fd(), reinterpret_cast<sockaddr*>(&address), &length);
    return ntohs(address.sin_port);
}

class ServerProcess {
public:
    explicit ServerProcess(int fixed_port = 0, const std::string& tag = "")
        : port_(fixed_port != 0 ? fixed_port : find_free_port()) {
        log_path_ = "logs/test_" + std::to_string(port_) + tag + ".log";
        ::unlink(log_path_.c_str());
        process_ = std::make_unique<Process>(
            std::vector<std::string>{g_bin_dir + "/server", std::to_string(port_), log_path_}, false);
        if (!process_->read_until("Server listening on 0.0.0.0:" + std::to_string(port_)))
            throw std::runtime_error("server did not start: " + process_->output());
    }
    int port() const { return port_; }
    Process& process() { return *process_; }

    void wait_for_log(const std::string& text) {
        if (!process_->read_until(text)) throw std::runtime_error("server log never showed: " + text);
    }
    bool log_contains(const std::string& text) const {
        return process_->output().find(text) != std::string::npos;
    }

    int stop() {
        if (stopped_) return exit_code_;
        process_->send_signal(SIGTERM);
        process_->read_all();
        exit_code_ = process_->exit_code();
        stopped_ = true;
        return exit_code_;
    }
    ~ServerProcess() {
        if (!stopped_) { process_->send_signal(SIGTERM); process_->read_all(); }
        ::unlink(log_path_.c_str());
    }

private:
    int port_;
    std::string log_path_;
    std::unique_ptr<Process> process_;
    bool stopped_ = false;
    int exit_code_ = -1;
};


class TestConn {
public:
    explicit TestConn(int port) : framer_(kMaxResponseBytes) {
        socket_ = Socket(::socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<uint16_t>(port));
        if (::connect(socket_.fd(), reinterpret_cast<sockaddr*>(&address), sizeof address) != 0)
            throw testing::Failure(std::string("connect failed: ") + std::strerror(errno));
        const int one = 1;
        ::setsockopt(socket_.fd(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }

    void send_raw(const std::string& bytes) {
        std::string error;
        if (!send_all(socket_.fd(), bytes, error)) throw testing::Failure("send failed: " + error);
    }
    void send_request(const std::string& id, const std::string& command, const std::string& payload = "{}") {
        send_raw(request_text(id, command, payload));
    }
    static std::string request_text(const std::string& id, const std::string& command,
                                    const std::string& payload = "{}") {
        return "{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"" + id + "\",\"command\":\"" +
               command + "\",\"payload\":" + payload + "}\n";
    }

    Json read_response(std::string* raw_line = nullptr) {
        while (true) {
            Frame frame;
            if (framer_.next_frame(frame)) {
                if (frame.status != FrameStatus::Message) continue;
                if (raw_line) *raw_line = frame.line;
                Json json;
                std::string error;
                if (!Json::parse(frame.line, json, error))
                    throw testing::Failure("server sent unparseable JSON: " + error);
                return json;
            }
            pollfd p{socket_.fd(), POLLIN, 0};
            if (::poll(&p, 1, kWatchdogMs) <= 0) throw testing::Failure("watchdog: no response from server");
            char buffer[4096];
            const ssize_t n = ::recv(socket_.fd(), buffer, sizeof buffer, 0);
            if (n == 0) throw testing::Failure("server closed the connection unexpectedly");
            if (n < 0) throw testing::Failure(std::string("recv failed: ") + std::strerror(errno));
            framer_.feed(buffer, static_cast<std::size_t>(n));
        }
    }

    bool next_is_eof() {
        char buffer[16];
        pollfd p{socket_.fd(), POLLIN, 0};
        if (::poll(&p, 1, kWatchdogMs) <= 0) return false;
        return ::recv(socket_.fd(), buffer, sizeof buffer, 0) == 0;
    }

    Json call(const std::string& id, const std::string& command, const std::string& payload = "{}") {
        send_request(id, command, payload);
        Json response = read_response();
        const Json* echoed = response.find("request_id");
        if (!echoed || !echoed->is_string() || echoed->string_value() != id)
            throw testing::Failure("response did not echo request_id " + id);
        return response;
    }
    Json hello(const std::string& name) {
        return call("hello", "HELLO", "{\"display_name\":\"" + name + "\"}");
    }

    void abort_connection() {
        linger hard{1, 0};
        ::setsockopt(socket_.fd(), SOL_SOCKET, SO_LINGER, &hard, sizeof hard);
        socket_.close_now();
    }
    void close() { socket_.close_now(); }

private:
    Socket socket_;
    MessageFramer framer_;
};


inline std::string status_of(const Json& r) { return r.find("status")->string_value(); }
inline std::string code_of(const Json& r) {
    const Json* code = r.find("error_code");
    return code ? code->string_value() : "";
}
inline const Json& payload_of(const Json& r) { return *r.find("payload"); }
inline long long list_count(TestConn& c, const std::string& id) {
    return payload_of(c.call(id, "LIST_TASKS")).find("count")->int_value();
}
inline bool no_internals(const Json& r) {
    const std::string text = r.dump();
    return text.find(".cpp") == std::string::npos && text.find("/home") == std::string::npos &&
           text.find("stack") == std::string::npos && text.find("terminate") == std::string::npos;
}

struct ClientRun { std::string output; int exit_code; };
inline ClientRun run_client(const std::vector<std::string>& args, const std::string& input) {
    std::vector<std::string> argv = {g_bin_dir + "/client"};
    argv.insert(argv.end(), args.begin(), args.end());
    Process client(argv, true);
    client.write_stdin(input);
    client.close_stdin();
    client.read_all();
    ClientRun result;
    result.output = client.output();
    result.exit_code = client.exit_code();
    return result;
}

inline bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}

