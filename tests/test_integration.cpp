// test_integration.cpp -- end-to-end tests over real TCP sockets.
//
// The test starts the real bin/server and bin/client programs as child
// processes and talks to them. No test uses sleep(): the server is "ready"
// when its "listening" log line has been read from its stdout pipe, and every
// wait is a blocking read for an expected response or log line. The only
// timeout is poll() inside read helpers, used purely as a watchdog so that a
// broken build fails instead of hanging.
//
//   usage: test_integration [bin_dir]      (default: bin)
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

// -------------------------------------------------------------- processes

// A child process with stdout+stderr captured through one pipe and
// (optionally) stdin fed through another.
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
        std::size_t done = 0;  // a pipe, not a socket: use write(), not send()
        while (done < text.size()) {
            const ssize_t n = ::write(in_fd_, text.data() + done, text.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("write to child failed");
            done += static_cast<std::size_t>(n);
        }
    }
    void close_stdin() { if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; } }

    // Reads output until `marker` has appeared (or EOF). Returns everything
    // read so far in `captured`. Blocking, with a watchdog.
    bool read_until(const std::string& marker, std::size_t from = 0) {
        while (captured_.find(marker, from) == std::string::npos) {
            if (!read_more()) return false;
        }
        return true;
    }
    void read_all() { while (read_more()) {} }
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

int find_free_port() {
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

// A running STCS server. Ready means its "listening" line was printed.
class ServerProcess {
public:
    ServerProcess() : port_(find_free_port()) {
        log_path_ = "logs/test_" + std::to_string(port_) + ".log";
        ::unlink(log_path_.c_str());
        process_ = std::make_unique<Process>(
            std::vector<std::string>{g_bin_dir + "/server", std::to_string(port_), log_path_}, false);
        if (!process_->read_until("Server listening on 0.0.0.0:" + std::to_string(port_)))
            throw std::runtime_error("server did not start: " + process_->output());
    }
    int port() const { return port_; }
    Process& process() { return *process_; }

    // Blocks until the server has logged `text` (after anything already seen).
    void wait_for_log(const std::string& text) {
        if (!process_->read_until(text)) throw std::runtime_error("server log never showed: " + text);
    }
    bool log_contains(const std::string& text) const {
        return process_->output().find(text) != std::string::npos;
    }

    // SIGTERM, then read the rest of the log until the server exits.
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
        ::unlink(log_path_.c_str());  // keep logs/ tidy: test logs are throw-away
    }

private:
    int port_;
    std::string log_path_;
    std::unique_ptr<Process> process_;
    bool stopped_ = false;
    int exit_code_ = -1;
};

// ------------------------------------------------------------ test client

// A raw TCP client: it can send arbitrary bytes in arbitrary pieces, and it
// reads responses through the same framer the real client uses.
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
        const int one = 1;  // send small writes immediately: encourages real splits
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

    // Next response line, parsed. Fails the test on timeout or EOF.
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

    // True if the next thing from the server is EOF (used after QUIT).
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

    void abort_connection() {  // RST instead of FIN: simulates a crashed client
        linger hard{1, 0};
        ::setsockopt(socket_.fd(), SOL_SOCKET, SO_LINGER, &hard, sizeof hard);
        socket_.close_now();
    }
    void close() { socket_.close_now(); }

private:
    Socket socket_;
    MessageFramer framer_;
};

// ----------------------------------------------------------------- checks

std::string status_of(const Json& r) { return r.find("status")->string_value(); }
std::string code_of(const Json& r) {
    const Json* code = r.find("error_code");
    return code ? code->string_value() : "";
}
const Json& payload_of(const Json& r) { return *r.find("payload"); }
long long list_count(TestConn& c, const std::string& id) {
    return payload_of(c.call(id, "LIST_TASKS")).find("count")->int_value();
}
bool no_internals(const Json& r) {
    const std::string text = r.dump();
    return text.find(".cpp") == std::string::npos && text.find("/home") == std::string::npos &&
           text.find("stack") == std::string::npos && text.find("terminate") == std::string::npos;
}

// Runs the real client with scripted stdin and returns its merged output.
struct ClientRun { std::string output; int exit_code; };
ClientRun run_client(const std::vector<std::string>& args, const std::string& input) {
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

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) g_bin_dir = argv[1];
    ::signal(SIGPIPE, SIG_IGN);
    ::alarm(300);  // global watchdog for the whole run

    // ============================================================ NORMAL
    testing::run("T-01", "server start; connect; HELLO -> READY with session_id, version 1, commands", [] {
        ServerProcess server;
        TestConn client(server.port());
        server.wait_for_log("connected from 127.0.0.1");
        const Json r = client.hello("Ana");
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK(!payload_of(r).find("session_id")->string_value().empty());
        CHECK_EQ(payload_of(r).find("display_name")->string_value(), std::string("Ana"));
        CHECK_EQ(payload_of(r).find("server_version")->int_value(), 1);
        CHECK_EQ(payload_of(r).find("commands")->items().size(), 9u);
        server.wait_for_log("HELLO success");
        CHECK(server.log_contains("Client sess-0001 connected from 127.0.0.1"));
        client.call("q", "QUIT");
    });

    testing::run("T-02", "CREATE_TASK returns id 1, OPEN, null owner, created_by, timestamp", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        const Json r = c.call("r2", "CREATE_TASK", "{\"title\":\"Write methodology section\",\"priority\":\"HIGH\"}");
        CHECK_EQ(status_of(r), std::string("OK"));
        const Json& task = *payload_of(r).find("task");
        CHECK_EQ(task.find("task_id")->int_value(), 1);
        CHECK_EQ(task.find("status")->string_value(), std::string("OPEN"));
        CHECK(task.find("claimed_by")->is_null());
        CHECK_EQ(task.find("created_by")->string_value(), std::string("Ana"));
        CHECK_EQ(task.find("priority")->string_value(), std::string("HIGH"));
        const std::string stamp = task.find("created_at")->string_value();
        CHECK_EQ(stamp.size(), 20u);
        CHECK(stamp.back() == 'Z' && stamp[10] == 'T');
        server.wait_for_log("CREATE_TASK success task_id=1");
        c.call("q", "QUIT");
    });

    testing::run("T-03", "LIST_TASKS: ascending ids, all fields, status filter", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        c.call("a", "CREATE_TASK", "{\"title\":\"open one\"}");
        c.call("b", "CREATE_TASK", "{\"title\":\"claimed one\",\"priority\":\"LOW\"}");
        c.call("c", "CREATE_TASK", "{\"title\":\"done one\",\"priority\":\"HIGH\"}");
        c.call("d", "CLAIM_TASK", "{\"task_id\":2}");
        c.call("e", "COMPLETE_TASK", "{\"task_id\":3}");
        const Json all = c.call("f", "LIST_TASKS");
        CHECK_EQ(payload_of(all).find("count")->int_value(), 3);
        const auto& tasks = payload_of(all).find("tasks")->items();
        CHECK_EQ(tasks[0].find("task_id")->int_value(), 1);
        CHECK_EQ(tasks[1].find("task_id")->int_value(), 2);
        CHECK_EQ(tasks[2].find("task_id")->int_value(), 3);
        for (const char* field : {"task_id", "title", "priority", "status", "claimed_by", "created_by", "created_at"})
            CHECK(tasks[0].find(field) != nullptr);
        const Json open = c.call("g", "LIST_TASKS", "{\"status\":\"OPEN\"}");
        CHECK_EQ(payload_of(open).find("count")->int_value(), 1);
        CHECK_EQ(payload_of(open).find("tasks")->items()[0].find("title")->string_value(), std::string("open one"));
        c.call("q", "QUIT");
    });

    testing::run("T-04", "lifecycle: claim, release, claim, done, done again, delete, list, quit", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        c.call("a", "CREATE_TASK", "{\"title\":\"one\"}");
        c.call("b", "CREATE_TASK", "{\"title\":\"two\"}");
        Json r = c.call("c1", "CLAIM_TASK", "{\"task_id\":1}");
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK_EQ(payload_of(r).find("task")->find("claimed_by")->string_value(), std::string("Ana"));
        r = c.call("c2", "RELEASE_TASK", "{\"task_id\":1}");
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK_EQ(payload_of(r).find("task")->find("status")->string_value(), std::string("OPEN"));
        CHECK(payload_of(r).find("task")->find("claimed_by")->is_null());
        c.call("c3", "CLAIM_TASK", "{\"task_id\":1}");
        r = c.call("c4", "COMPLETE_TASK", "{\"task_id\":1}");
        CHECK_EQ(payload_of(r).find("task")->find("status")->string_value(), std::string("DONE"));
        CHECK_EQ(payload_of(r).find("task")->find("claimed_by")->string_value(), std::string("Ana"));
        r = c.call("c5", "COMPLETE_TASK", "{\"task_id\":1}");
        CHECK_EQ(code_of(r), std::string("CONFLICT"));
        r = c.call("c6", "DELETE_TASK", "{\"task_id\":2}");
        CHECK(payload_of(r).find("deleted")->bool_value());
        CHECK_EQ(list_count(c, "l"), 1);
        r = c.call("q", "QUIT");
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK(c.next_is_eof());                       // server closed the socket after the reply
        server.wait_for_log("disconnected gracefully");
        server.wait_for_log("session released");
    });

    // ============================================================ INVALID
    testing::run("T-05", "unknown command MAKE_COFFEE: UNKNOWN_COMMAND, next command still works", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        const Json r = c.call("r5", "MAKE_COFFEE");
        CHECK_EQ(status_of(r), std::string("ERROR"));
        CHECK_EQ(code_of(r), std::string("UNKNOWN_COMMAND"));
        CHECK_EQ(status_of(c.call("r6", "LIST_TASKS")), std::string("OK"));
        server.wait_for_log("UNKNOWN_COMMAND");
        c.call("q", "QUIT");
    });

    testing::run("T-06", "missing field, invalid fields, NOT_FOUND, bad JSON, empty line; state unchanged", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        c.call("seed", "CREATE_TASK", "{\"title\":\"the only task\"}");

        Json r = c.call("a", "CREATE_TASK", "{}");                                   // (a)
        CHECK_EQ(code_of(r), std::string("MISSING_FIELD"));
        CHECK_EQ(payload_of(r).find("field")->string_value(), std::string("title"));
        CHECK(no_internals(r));
        r = c.call("b", "CREATE_TASK", "{\"title\":\"t\",\"priority\":\"URGENT\"}"); // (b)
        CHECK_EQ(code_of(r), std::string("INVALID_FIELD"));
        r = c.call("c", "CLAIM_TASK", "{\"task_id\":\"abc\"}");                      // (c)
        CHECK_EQ(code_of(r), std::string("INVALID_FIELD"));
        r = c.call("d", "CLAIM_TASK", "{\"task_id\":999}");                          // (d)
        CHECK_EQ(code_of(r), std::string("NOT_FOUND"));
        CHECK(no_internals(r));

        c.send_raw("{\"version\":1,\n");                                              // (e)
        r = c.read_response();
        CHECK_EQ(code_of(r), std::string("INVALID_MESSAGE"));
        CHECK(r.find("request_id")->is_null());
        c.send_raw("\n");                                                             // (f)
        r = c.read_response();
        CHECK_EQ(code_of(r), std::string("INVALID_MESSAGE"));

        r = c.call("s", "LIST_TASKS", "{\"status\":\"INVALID\"}");
        CHECK_EQ(code_of(r), std::string("INVALID_FIELD"));
        CHECK_EQ(list_count(c, "z"), 1);              // still exactly one, unchanged task
        c.call("q", "QUIT");
    });

    testing::run("X-05", "command before HELLO: INVALID_STATE; HELP allowed; failed HELLO retried", [] {
        ServerProcess server;
        TestConn c(server.port());
        CHECK_EQ(code_of(c.call("a", "LIST_TASKS")), std::string("INVALID_STATE"));
        CHECK_EQ(code_of(c.call("b", "CREATE_TASK", "{\"title\":\"x\"}")), std::string("INVALID_STATE"));
        CHECK_EQ(status_of(c.call("c", "HELP")), std::string("OK"));
        CHECK_EQ(code_of(c.call("d", "HELLO", "{\"display_name\":\"bad!\"}")), std::string("INVALID_FIELD"));
        CHECK_EQ(code_of(c.call("e", "LIST_TASKS")), std::string("INVALID_STATE"));   // still CONNECTED
        CHECK_EQ(status_of(c.call("f", "HELLO", "{\"display_name\":\"Ana\"}")), std::string("OK"));
        CHECK_EQ(code_of(c.call("g", "HELLO", "{\"display_name\":\"Ana\"}")), std::string("INVALID_STATE"));
        CHECK_EQ(status_of(c.call("h", "LIST_TASKS")), std::string("OK"));            // connection survived
        c.call("q", "QUIT");
    });

    testing::run("U-I1", "envelope errors over TCP: version, type, request_id recovery", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        c.send_raw("{\"version\":2,\"type\":\"REQUEST\",\"request_id\":\"v\",\"command\":\"LIST_TASKS\",\"payload\":{}}\n");
        Json r = c.read_response();
        CHECK_EQ(code_of(r), std::string("INVALID_VERSION"));
        CHECK_EQ(r.find("request_id")->string_value(), std::string("v"));
        c.send_raw("{\"version\":1,\"request_id\":\"t\",\"command\":\"QUIT\",\"payload\":{}}\n");
        r = c.read_response();
        CHECK_EQ(code_of(r), std::string("MISSING_FIELD"));
        CHECK_EQ(payload_of(r).find("field")->string_value(), std::string("type"));
        c.send_raw("{\"version\":1,\"type\":\"REQUEST\",\"command\":\"QUIT\",\"payload\":{}}\n");
        r = c.read_response();
        CHECK_EQ(code_of(r), std::string("MISSING_FIELD"));
        CHECK(r.find("request_id")->is_null());       // unrecoverable -> null
        c.send_raw("[1,2,3]\n");
        CHECK_EQ(code_of(c.read_response()), std::string("INVALID_MESSAGE"));
        c.send_raw("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"u\",\"command\":\"LIST_TASKS\",\"payload\":{},\"future_field\":1}\n");
        CHECK_EQ(status_of(c.read_response()), std::string("OK"));   // unknown extra fields ignored
        c.send_raw(std::string("{\"version\":1,\"x\":\"bad\xFF\"}\n"));
        CHECK_EQ(code_of(c.read_response()), std::string("INVALID_MESSAGE"));  // invalid UTF-8
        c.call("q", "QUIT");
    });

    testing::run("U-I2", "oversize request (20 KiB): one MESSAGE_TOO_LARGE, then stream resynchronised", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        // One huge line, then a valid request, in a single send().
        c.send_raw(std::string(20000, 'A') + "\n" + TestConn::request_text("after", "LIST_TASKS"));
        Json r = c.read_response();
        CHECK_EQ(code_of(r), std::string("MESSAGE_TOO_LARGE"));
        r = c.read_response();                                   // the very next response ...
        CHECK_EQ(r.find("request_id")->string_value(), std::string("after"));  // ... is for the valid request
        CHECK_EQ(status_of(r), std::string("OK"));
        // A long line that arrives without a newline for a while, then ends.
        c.send_raw(std::string(30000, 'B'));
        c.send_raw(std::string(30000, 'C') + "\n");
        CHECK_EQ(code_of(c.read_response()), std::string("MESSAGE_TOO_LARGE"));
        CHECK_EQ(status_of(c.call("again", "LIST_TASKS")), std::string("OK"));
        c.call("q", "QUIT");
    });

    testing::run("U-I3", "LIST_TASKS reply larger than 8192 bytes is delivered intact (clarification C-1)", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        const std::string title(120, 'T');
        for (int i = 0; i < 60; ++i)
            CHECK_EQ(status_of(c.call("c" + std::to_string(i), "CREATE_TASK", "{\"title\":\"" + title + "\"}")), std::string("OK"));
        c.send_request("big", "LIST_TASKS");
        std::string raw;
        const Json r = c.read_response(&raw);
        CHECK(raw.size() > kMaxMessageBytes);
        CHECK_EQ(payload_of(r).find("count")->int_value(), 60);
        CHECK_EQ(payload_of(r).find("tasks")->items().size(), 60u);
        c.call("q", "QUIT");
    });

    // ======================================================== FRAMING (TCP)
    testing::run("T-13", "partial message over TCP: processed once, only after the newline", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        const std::string wire = TestConn::request_text("split1", "CREATE_TASK",
                                                        "{\"title\":\"Test\",\"priority\":\"HIGH\"}");
        // Phase 1 example: three pieces, the last being just the newline.
        const std::size_t cut = wire.find("\"command\"");
        c.send_raw(wire.substr(0, cut));
        c.send_raw(wire.substr(cut, wire.size() - 1 - cut));
        c.send_raw("\n");
        Json r = c.read_response();
        // If the server had acted on a fragment, the first response here would
        // be an INVALID_MESSAGE for that fragment instead of this reply.
        CHECK_EQ(r.find("request_id")->string_value(), std::string("split1"));
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK_EQ(list_count(c, "l1"), 1);              // exactly one task, no duplicates

        // Split inside a multi-byte UTF-8 character.
        const std::string accented = TestConn::request_text("split2", "CREATE_TASK",
                                                            "{\"title\":\"Caf\xC3\xA9 \xE2\x9C\x93\"}");
        const std::size_t inside = accented.find("\xC3\xA9") + 1;   // between the 2 bytes of e-acute
        c.send_raw(accented.substr(0, inside));
        c.send_raw(accented.substr(inside));
        r = c.read_response();
        CHECK_EQ(r.find("request_id")->string_value(), std::string("split2"));
        CHECK_EQ(payload_of(r).find("task")->find("title")->string_value(), std::string("Caf\xC3\xA9 \xE2\x9C\x93"));

        // Every byte as its own send().
        const std::string slow = TestConn::request_text("split3", "CREATE_TASK", "{\"title\":\"byte by byte\"}");
        for (char byte : slow) c.send_raw(std::string(1, byte));
        r = c.read_response();
        CHECK_EQ(r.find("request_id")->string_value(), std::string("split3"));
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK_EQ(list_count(c, "l2"), 3);
        c.call("q", "QUIT");
    });

    testing::run("T-14", "two messages in one send; two plus a trailing fragment", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        c.send_raw(TestConn::request_text("A", "CREATE_TASK", "{\"title\":\"A\"}") +
                   TestConn::request_text("B", "CREATE_TASK", "{\"title\":\"B\"}"));
        Json r1 = c.read_response();
        Json r2 = c.read_response();
        CHECK_EQ(r1.find("request_id")->string_value(), std::string("A"));   // in order
        CHECK_EQ(r2.find("request_id")->string_value(), std::string("B"));
        CHECK_EQ(payload_of(r1).find("task")->find("task_id")->int_value(), 1);
        CHECK_EQ(payload_of(r2).find("task")->find("task_id")->int_value(), 2);

        const std::string third = TestConn::request_text("C", "CREATE_TASK", "{\"title\":\"C\"}");
        const std::size_t keep = third.size() - 9;
        c.send_raw(TestConn::request_text("D", "CREATE_TASK", "{\"title\":\"D\"}") +
                   TestConn::request_text("E", "CREATE_TASK", "{\"title\":\"E\"}") + third.substr(0, keep));
        CHECK_EQ(c.read_response().find("request_id")->string_value(), std::string("D"));
        CHECK_EQ(c.read_response().find("request_id")->string_value(), std::string("E"));
        c.send_raw(third.substr(keep));                          // the fragment completes
        r1 = c.read_response();
        CHECK_EQ(r1.find("request_id")->string_value(), std::string("C"));
        CHECK_EQ(status_of(r1), std::string("OK"));
        CHECK_EQ(list_count(c, "n"), 5);                         // A,B,D,E,C: no loss, no extra
        c.call("q", "QUIT");
    });

    // ====================================================== NETWORK FAILURES
    testing::run("T-09", "client exits unexpectedly (RST, then plain close): logged, session released, server keeps listening", [] {
        ServerProcess server;
        {
            TestConn doomed(server.port());
            doomed.hello("Ana");
            doomed.call("a", "CREATE_TASK", "{\"title\":\"survives the crash\"}");
            doomed.abort_connection();                            // no QUIT
        }
        server.wait_for_log("disconnected unexpectedly");
        server.wait_for_log("sess-0001 session released");
        {
            TestConn next(server.port());                         // the server went back to accept()
            CHECK_EQ(status_of(next.hello("Ben")), std::string("OK"));
            const Json list = next.call("l", "LIST_TASKS");
            CHECK_EQ(payload_of(list).find("count")->int_value(), 1);   // the task is still there
            next.close();                                          // plain close, also without QUIT
        }
        server.wait_for_log("sess-0002 session released");
        CHECK(server.log_contains("sess-0002 disconnected unexpectedly"));
        TestConn third(server.port());
        CHECK_EQ(status_of(third.hello("Cara")), std::string("OK"));
        third.call("q", "QUIT");
    });

    testing::run("T-09b", "client dies in the middle of a message: fragment discarded and logged", [] {
        ServerProcess server;
        {
            TestConn c(server.port());
            c.hello("Ana");
            c.send_raw("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"half\"");
            c.close();
        }
        server.wait_for_log("bytes of an incomplete message discarded");
        TestConn next(server.port());
        CHECK_EQ(status_of(next.hello("Ana")), std::string("OK"));
        next.call("q", "QUIT");
    });

    testing::run("X-04", "tasks survive QUIT and reconnect while the server keeps running (FR-12)", [] {
        ServerProcess server;
        {
            TestConn c(server.port());
            c.hello("Ana");
            c.call("a", "CREATE_TASK", "{\"title\":\"persist me\",\"priority\":\"LOW\"}");
            c.call("b", "CLAIM_TASK", "{\"task_id\":1}");
            c.call("q", "QUIT");
        }
        server.wait_for_log("sess-0001 session released");
        TestConn again(server.port());
        again.hello("Ben");
        const Json list = again.call("l", "LIST_TASKS");
        CHECK_EQ(payload_of(list).find("count")->int_value(), 1);
        const Json& task = payload_of(list).find("tasks")->items()[0];
        CHECK_EQ(task.find("title")->string_value(), std::string("persist me"));
        CHECK_EQ(task.find("claimed_by")->string_value(), std::string("Ana"));   // claims remain after QUIT (Q2)
        again.call("q", "QUIT");
    });

    testing::run("X-01", "a second client waits in the backlog and is served after the first quits", [] {
        ServerProcess server;
        TestConn first(server.port());
        first.hello("Ana");
        TestConn second(server.port());               // TCP connect succeeds (kernel backlog)
        second.send_request("h2", "HELLO", "{\"display_name\":\"Ben\"}");
        first.call("a", "CREATE_TASK", "{\"title\":\"from first\"}");   // first is still served
        first.call("q", "QUIT");
        const Json r = second.read_response();         // now the server accept()s the second client
        CHECK_EQ(r.find("request_id")->string_value(), std::string("h2"));
        CHECK_EQ(status_of(r), std::string("OK"));
        CHECK_EQ(payload_of(r).find("session_id")->string_value(), std::string("sess-0002"));
        CHECK_EQ(list_count(second, "l"), 1);
        second.call("q", "QUIT");
    });

    testing::run("X-02", "SIGTERM while a client is connected: session closed, shutdown logged, exit 0", [] {
        ServerProcess server;
        TestConn c(server.port());
        c.hello("Ana");
        server.process().send_signal(SIGTERM);
        CHECK(c.next_is_eof());                        // the server closed the connection
        const int code = server.stop();
        CHECK_EQ(code, 0);
        CHECK(server.log_contains("sess-0001 closed by server shutdown"));
        CHECK(server.log_contains("Shutdown requested"));
        CHECK(server.log_contains("Server shutdown complete"));
    });

    testing::run("X-03", "SIGINT with no client connected: clean shutdown, exit 0", [] {
        ServerProcess server;
        server.process().send_signal(SIGINT);
        server.process().read_all();
        CHECK_EQ(server.process().exit_code(), 0);
        CHECK(server.log_contains("Server shutdown complete"));
    });

    testing::run("U-N1", "server rejects bad arguments (port 0, text, missing) without starting", [] {
        for (const std::vector<std::string>& args :
             {std::vector<std::string>{}, {"0"}, {"abc"}, {"70000"}, {"-1"}}) {
            std::vector<std::string> argv = {g_bin_dir + "/server"};
            argv.insert(argv.end(), args.begin(), args.end());
            Process p(argv, false);
            p.read_all();
            CHECK_EQ(p.exit_code(), 2);
            CHECK(contains(p.output(), "Usage:"));
        }
    });

    testing::run("U-N2", "server reports a port that is already in use and exits 1", [] {
        ServerProcess first;
        Process second({g_bin_dir + "/server", std::to_string(first.port()), "logs/test_dup.log"}, false);
        second.read_all();
        CHECK_EQ(second.exit_code(), 1);
        CHECK(contains(second.output(), "bind to port"));
        ::unlink("logs/test_dup.log");
    });

    // ============================================================== CLIENT
    testing::run("T-07", "server not running: client reports failure clearly, exit 1, no crash", [] {
        const int port = find_free_port();             // nothing listens here
        const ClientRun run = run_client({"127.0.0.1", std::to_string(port), "Ana"}, "");
        CHECK_EQ(run.exit_code, 1);
        CHECK(contains(run.output, "Could not connect"));
        CHECK(contains(run.output, "Is the server running?"));
        CHECK(!contains(run.output, "terminate"));
        CHECK(!contains(run.output, "Segmentation"));
    });

    testing::run("T-08", "client rejects invalid ports/names before connecting (exit 2)", [] {
        for (const char* port : {"70000", "0", "abc", "-5", "", "65536", "50.5"}) {
            const ClientRun run = run_client({"127.0.0.1", port, "Ana"}, "");
            CHECK_EQ(run.exit_code, 2);
            CHECK(contains(run.output, "invalid port"));
            CHECK(!contains(run.output, "Connecting"));       // no connection was attempted
        }
        const ClientRun name = run_client({"127.0.0.1", "5050", "bad!name"}, "");
        CHECK_EQ(name.exit_code, 2);
        CHECK(contains(name.output, "invalid display name"));
        const ClientRun usage = run_client({"127.0.0.1"}, "");
        CHECK_EQ(usage.exit_code, 2);
        CHECK(contains(usage.output, "Usage:"));
    });

    testing::run("T-08b", "unresolvable host: plain-language message, exit 1", [] {
        const ClientRun run = run_client({"not.a.real.host.invalid", "5050", "Ana"}, "");
        CHECK_EQ(run.exit_code, 1);
        CHECK(contains(run.output, "Could not find host"));
    });

    testing::run("X-06", "empty command: client re-prompts and the session continues", [] {
        ServerProcess server;
        const ClientRun run = run_client({"127.0.0.1", std::to_string(server.port()), "Ana"},
                                         "\n   \nlist\nquit\n");
        CHECK_EQ(run.exit_code, 0);
        CHECK(contains(run.output, "Enter a command"));
        CHECK(contains(run.output, "No tasks to show."));
        CHECK(contains(run.output, "Session closed. Goodbye."));
    });

    testing::run("T-04c", "CLI full scenario: add, list, claim, release, claim, done, delete, errors, quit", [] {
        ServerProcess server;
        const ClientRun run = run_client(
            {"127.0.0.1", std::to_string(server.port()), "Ana"},
            "help\nadd\nWrite methodology section\nhigh\nadd Draft abstract\nlist\nclaim 1\nrelease 1\n"
            "claim 1\ndone 1\nlist\nlist open\nclaim 1\nclaim 9999\nclaim abc\nmake_coffee\n"
            "raw MAKE_COFFEE\nraw CREATE_TASK {\"priority\":\"HIGH\"}\nhelp server CLAIM_TASK\n"
            "delete 2\nlist\nquit\n");
        CHECK_EQ(run.exit_code, 0);
        for (const char* expected :
             {"Session sess-0001 started as 'Ana'.", "Created task 1.", "Created task 2.", "Claimed task 1.",
              "Released task 1 (released by Ana).", "Marked task 1 as done.", "Deleted task 2.",
              "Error [CONFLICT]: Task 1 is already marked done.", "Error [NOT_FOUND]: No task exists with id 9999.",
              "must be a whole number", "Unknown command 'make_coffee'", "Error [UNKNOWN_COMMAND]",
              "Error [MISSING_FIELD]: Field 'title' is required.", "CLAIM_TASK: Take ownership of an OPEN task.",
              "Session closed. Goodbye."})
            CHECK(contains(run.output, expected));
        CHECK(!contains(run.output, "terminate"));
        server.wait_for_log("disconnected gracefully");
        CHECK(server.log_contains("CREATE_TASK success task_id=1"));
        CHECK(server.log_contains("CLAIM_TASK rejected error=NOT_FOUND task_id=9999"));
        CHECK(!server.log_contains("Write methodology section"));     // titles are not logged
    });

    testing::run("X-07", "stdin ends without quit: client sends QUIT itself and exits 0", [] {
        ServerProcess server;
        const ClientRun run = run_client({"127.0.0.1", std::to_string(server.port()), "Ana"}, "list\n");
        CHECK_EQ(run.exit_code, 0);
        CHECK(contains(run.output, "End of input"));
        server.wait_for_log("disconnected gracefully");
    });

    testing::run("X-08", "re-prompts for bad title and priority inside 'add'; /cancel aborts", [] {
        ServerProcess server;
        const ClientRun run = run_client({"127.0.0.1", std::to_string(server.port()), "Ana"},
                                         "add\n   \nOK title\nurgent\nlow\nadd\n/cancel\nlist\nquit\n");
        CHECK_EQ(run.exit_code, 0);
        CHECK(contains(run.output, "must not be blank"));
        CHECK(contains(run.output, "Priority must be LOW, MEDIUM or HIGH"));
        CHECK(contains(run.output, "Created task 1."));
        CHECK(contains(run.output, "Cancelled."));
        CHECK(contains(run.output, "1 task"));
    });

    testing::run("T-17", "server disappears while the client is active: clean message, exit 1, no crash", [] {
        ServerProcess server;
        Process client({g_bin_dir + "/client", "127.0.0.1", std::to_string(server.port()), "Ana"}, true);
        CHECK(client.read_until("stcs> "));
        client.write_stdin("add First task\n");
        CHECK(client.read_until("Created task 1."));
        CHECK_EQ(server.stop(), 0);                      // the server is gone now
        client.write_stdin("list\n");
        client.read_all();
        const int status = client.wait_status();
        CHECK(WIFEXITED(status));                        // not killed by a signal
        CHECK_EQ(WEXITSTATUS(status), 1);
        CHECK(contains(client.output(), "Error:"));
        CHECK(contains(client.output(), "The session has ended"));
        CHECK(!contains(client.output(), "terminate"));
    });

    return testing::summarize("test_integration");
}
