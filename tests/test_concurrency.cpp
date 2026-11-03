// Phase 3 tests: several clients against one real server process.
// Test ids: C-xx = concurrent operation tests, R-xx = reliability tests.
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "tests/integration_support.hpp"

using namespace stcs;

namespace {

// Runs body(i) for i in [0, n) on n threads that all start at the same moment.
// The first failure raised in any thread is rethrown in the calling thread.
void parallel(int n, const std::function<void(int)>& body) {
    std::atomic<bool> go{false};
    std::mutex failure_mutex;
    std::string failure;
    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i) {
        threads.emplace_back([&, i] {
            while (!go.load()) std::this_thread::yield();
            try {
                body(i);
            } catch (const std::exception& problem) {
                std::lock_guard<std::mutex> lock(failure_mutex);
                if (failure.empty()) failure = problem.what();
            }
        });
    }
    go = true;
    for (std::thread& t : threads) t.join();
    if (!failure.empty()) throw testing::Failure("in client thread: " + failure);
}

std::string name_for(int i) { return "User" + std::to_string(i); }

std::string title_payload(const std::string& title) {
    return "{\"title\":\"" + title + "\"}";
}

std::string id_payload(long long id) { return "{\"task_id\":" + std::to_string(id) + "}"; }

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) g_bin_dir = argv[1];
    ::signal(SIGPIPE, SIG_IGN);
    ::alarm(300);

    // ---------------------------------------------------------------- concurrent operations

    testing::run("C-01", "three clients connect and HELLO at the same moment; unique session ids", [] {
        ServerProcess server;
        std::mutex m;
        std::set<std::string> ids;
        parallel(3, [&](int i) {
            TestConn c(server.port());
            const Json r = c.hello(name_for(i));
            CHECK_EQ(status_of(r), std::string("OK"));
            {
                std::lock_guard<std::mutex> lock(m);
                ids.insert(payload_of(r).find("session_id")->string_value());
            }
            // stay connected until all three have arrived, so they overlap
            for (int spin = 0; spin < 2000; ++spin) {
                { std::lock_guard<std::mutex> lock(m); if (ids.size() == 3) break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            c.call("q", "QUIT");
        });
        CHECK_EQ(ids.size(), 3u);
        server.wait_for_log("session released (active clients: 0)");
        CHECK(server.log_contains("(active clients: 3)"));
    });

    testing::run("C-02", "three clients perform three different operations at once", [] {
        ServerProcess server;
        TestConn seed(server.port());
        seed.hello("Seed");
        seed.call("s", "CREATE_TASK", title_payload("seeded"));
        parallel(3, [&](int i) {
            TestConn c(server.port());
            CHECK_EQ(status_of(c.hello(name_for(i))), std::string("OK"));
            if (i == 0) {
                CHECK_EQ(status_of(c.call("a", "CREATE_TASK", title_payload("from user0"))),
                         std::string("OK"));
            } else if (i == 1) {
                CHECK_EQ(status_of(c.call("a", "LIST_TASKS")), std::string("OK"));
            } else {
                CHECK_EQ(status_of(c.call("a", "CLAIM_TASK", id_payload(1))), std::string("OK"));
            }
            c.call("q", "QUIT");
        });
        CHECK_EQ(list_count(seed, "l"), 2);
        seed.call("q", "QUIT");
    });

    testing::run("C-03", "two clients modify the shared task list at once: no lost or duplicate ids", [] {
        ServerProcess server;
        std::mutex m;
        std::set<long long> ids;
        parallel(2, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            for (int k = 0; k < 40; ++k) {
                const Json r = c.call("r" + std::to_string(k), "CREATE_TASK", title_payload("t"));
                CHECK_EQ(status_of(r), std::string("OK"));
                std::lock_guard<std::mutex> lock(m);
                ids.insert(payload_of(r).find("task")->find("task_id")->int_value());
            }
            c.call("q", "QUIT");
        });
        CHECK_EQ(ids.size(), 80u);
        CHECK_EQ(*ids.begin(), 1);
        CHECK_EQ(*ids.rbegin(), 80);
        TestConn check(server.port());
        check.hello("Check");
        CHECK_EQ(list_count(check, "l"), 80);
        check.call("q", "QUIT");
    });

    testing::run("C-04", "conflict: 6 clients race to claim the same 30 tasks; each task has exactly one winner", [] {
        ServerProcess server;
        const int kClients = 6;
        const int kTasks = 30;
        {
            TestConn setup(server.port());
            setup.hello("Setup");
            for (int k = 0; k < kTasks; ++k) setup.call("c", "CREATE_TASK", title_payload("race"));
            setup.call("q", "QUIT");
        }
        std::mutex m;
        std::map<long long, int> winners;  // task -> number of OK replies
        std::atomic<int> conflicts{0};
        parallel(kClients, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            for (long long id = 1; id <= kTasks; ++id) {
                const Json r = c.call("k", "CLAIM_TASK", id_payload(id));
                if (status_of(r) == "OK") {
                    std::lock_guard<std::mutex> lock(m);
                    ++winners[id];
                } else {
                    CHECK_EQ(code_of(r), std::string("CONFLICT"));
                    ++conflicts;
                }
            }
            c.call("q", "QUIT");
        });
        CHECK_EQ(winners.size(), static_cast<std::size_t>(kTasks));
        for (const auto& entry : winners) CHECK_EQ(entry.second, 1);
        CHECK_EQ(conflicts.load(), kTasks * (kClients - 1));
        TestConn check(server.port());
        check.hello("Check");
        const Json list = check.call("l", "LIST_TASKS");
        for (const Json& task : payload_of(list).find("tasks")->items()) {
            CHECK_EQ(task.find("status")->string_value(), std::string("CLAIMED"));
            CHECK(!task.find("claimed_by")->is_null());
        }
        check.call("q", "QUIT");
    });

    testing::run("C-05", "overlap: one client deletes a task another client is viewing", [] {
        ServerProcess server;
        TestConn a(server.port());
        TestConn b(server.port());
        a.hello("Ana");
        b.hello("Ben");
        a.call("c", "CREATE_TASK", title_payload("shared"));
        CHECK_EQ(list_count(b, "l1"), 1);              // Ben is viewing it
        CHECK_EQ(status_of(a.call("d", "DELETE_TASK", id_payload(1))), std::string("OK"));
        const Json r = b.call("k", "CLAIM_TASK", id_payload(1));   // Ben acts on the stale view
        CHECK_EQ(code_of(r), std::string("NOT_FOUND"));
        CHECK_EQ(list_count(b, "l2"), 0);
        a.call("q", "QUIT");
        b.call("q", "QUIT");
    });

    testing::run("C-06", "6 clients HELLO with the same display name at once: exactly one wins", [] {
        ServerProcess server;
        std::atomic<int> ok{0};
        std::atomic<int> in_use{0};
        parallel(6, [&](int) {
            TestConn c(server.port());
            const Json r = c.hello("Same");
            if (status_of(r) == "OK") ++ok;
            else if (code_of(r) == "NAME_IN_USE") ++in_use;
            // keep the connection open until everyone has answered
            for (int spin = 0; spin < 2000 && ok + in_use < 6; ++spin)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        CHECK_EQ(ok.load(), 1);
        CHECK_EQ(in_use.load(), 5);
    });

    testing::run("C-07", "one client dies abruptly; the others keep working; its name is freed", [] {
        ServerProcess server;
        TestConn a(server.port());
        TestConn b(server.port());
        TestConn c(server.port());
        a.hello("Ana");
        b.hello("Ben");
        c.hello("Cara");
        a.call("t", "CREATE_TASK", title_payload("claimed by Ben"));
        b.call("k", "CLAIM_TASK", id_payload(1));
        b.send_raw("{\"version\":1,\"type\":\"REQ");   // dies in the middle of a message
        b.abort_connection();
        server.wait_for_log("disconnected unexpectedly");
        CHECK_EQ(status_of(a.call("t2", "CREATE_TASK", title_payload("after crash"))), std::string("OK"));
        CHECK_EQ(list_count(c, "l"), 2);
        TestConn b2(server.port());                      // Ben comes back with the same name
        CHECK_EQ(status_of(b2.hello("Ben")), std::string("OK"));
        a.call("q", "QUIT");
        c.call("q", "QUIT");
        b2.call("q", "QUIT");
    });

    testing::run("C-08", "one client sends invalid requests; the other clients are unaffected", [] {
        ServerProcess server;
        TestConn good1(server.port());
        TestConn good2(server.port());
        TestConn bad(server.port());
        good1.hello("Good1");
        good2.hello("Good2");
        bad.hello("Bad");
        bad.send_raw("this is not json\n");
        CHECK_EQ(code_of(bad.read_response()), std::string("INVALID_MESSAGE"));
        bad.send_raw("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"x\",\"command\":\"MAKE_COFFEE\",\"payload\":{}}\n");
        CHECK_EQ(code_of(bad.read_response()), std::string("UNKNOWN_COMMAND"));
        bad.send_raw("{\"version\":1,\"type\":\"REQUEST\",\"request_id\":\"y\",\"command\":\"CREATE_TASK\",\"payload\":{\"priority\":\"HIGH\"}}\n");
        CHECK_EQ(code_of(bad.read_response()), std::string("MISSING_FIELD"));
        bad.send_raw(std::string("{\"title\":\"\xff\xfe\"}\n"));            // invalid UTF-8
        CHECK_EQ(status_of(bad.read_response()), std::string("ERROR"));
        bad.send_raw(std::string(9000, 'x') + "\n");                        // over the 8192-byte limit
        CHECK_EQ(code_of(bad.read_response()), std::string("MESSAGE_TOO_LARGE"));
        CHECK_EQ(status_of(good1.call("a", "CREATE_TASK", title_payload("fine"))), std::string("OK"));
        CHECK_EQ(list_count(good2, "l"), 1);
        CHECK_EQ(list_count(bad, "l"), 1);     // the bad client's session is still usable too
        good1.call("q", "QUIT");
        good2.call("q", "QUIT");
        bad.call("q", "QUIT");
    });

    testing::run("C-09", "repeated connect/disconnect: 40 clean and 20 abrupt, server stays healthy", [] {
        ServerProcess server;
        TestConn keeper(server.port());
        keeper.hello("Keeper");
        for (int i = 0; i < 40; ++i) {
            TestConn c(server.port());
            CHECK_EQ(status_of(c.hello("Loop")), std::string("OK"));   // same name reused each time
            c.call("q", "QUIT");
        }
        for (int i = 0; i < 20; ++i) {
            TestConn c(server.port());
            if (i % 2 == 0) c.hello("Loop");
            c.abort_connection();
        }
        parallel(4, [&](int i) {
            for (int k = 0; k < 10; ++k) {
                TestConn c(server.port());
                c.hello(name_for(i));
                c.call("q", "QUIT");
            }
        });
        CHECK_EQ(status_of(keeper.call("t", "CREATE_TASK", title_payload("still alive"))), std::string("OK"));
        keeper.call("q", "QUIT");
        server.wait_for_log("sess-0001 session released (active clients: 0)");
    });

    testing::run("C-10", "rapid submission: 3 clients each pipeline 100 requests in one write", [] {
        ServerProcess server;
        parallel(3, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            std::string burst;
            for (int k = 0; k < 100; ++k)
                burst += TestConn::request_text("b" + std::to_string(k), "CREATE_TASK", title_payload("burst"));
            c.send_raw(burst);
            for (int k = 0; k < 100; ++k) {
                const Json r = c.read_response();
                CHECK_EQ(r.find("request_id")->string_value(), "b" + std::to_string(k));   // in order
                CHECK_EQ(status_of(r), std::string("OK"));
            }
            c.call("q", "QUIT");
        });
        TestConn check(server.port());
        check.hello("Check");
        CHECK_EQ(list_count(check, "l"), 300);
        check.call("q", "QUIT");
    });

    testing::run("C-11", "partial messages: two clients dribble requests in pieces at the same time", [] {
        ServerProcess server;
        parallel(2, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            for (int k = 0; k < 5; ++k) {
                const std::string text = TestConn::request_text("p" + std::to_string(k), "CREATE_TASK",
                                                               title_payload("piece"));
                for (std::size_t pos = 0; pos < text.size(); pos += 7) {
                    c.send_raw(text.substr(pos, 7));
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                const Json r = c.read_response();
                CHECK_EQ(r.find("request_id")->string_value(), "p" + std::to_string(k));
                CHECK_EQ(status_of(r), std::string("OK"));
            }
            c.call("q", "QUIT");
        });
    });

    testing::run("C-12", "several messages (plus a trailing fragment) in one TCP write, on 3 clients", [] {
        ServerProcess server;
        parallel(3, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            const std::string third = TestConn::request_text("m3", "LIST_TASKS");
            c.send_raw(TestConn::request_text("m1", "CREATE_TASK", title_payload("one")) +
                       TestConn::request_text("m2", "LIST_TASKS") + third.substr(0, 10));
            CHECK_EQ(c.read_response().find("request_id")->string_value(), std::string("m1"));
            CHECK_EQ(c.read_response().find("request_id")->string_value(), std::string("m2"));
            c.send_raw(third.substr(10));
            CHECK_EQ(c.read_response().find("request_id")->string_value(), std::string("m3"));
            c.call("q", "QUIT");
        });
    });

    // ---------------------------------------------------------------- reliability

    testing::run("R-01", "request for missing data and invalid state transitions are rejected cleanly", [] {
        ServerProcess server;
        TestConn a(server.port());
        TestConn b(server.port());
        a.send_request("early", "LIST_TASKS");              // before HELLO
        CHECK_EQ(code_of(a.read_response()), std::string("INVALID_STATE"));
        a.hello("Ana");
        b.hello("Ben");
        CHECK_EQ(code_of(a.hello("Ana")), std::string("INVALID_STATE"));        // second HELLO
        CHECK_EQ(code_of(a.call("m", "CLAIM_TASK", id_payload(9999))), std::string("NOT_FOUND"));
        CHECK_EQ(code_of(a.call("m", "DELETE_TASK", id_payload(9999))), std::string("NOT_FOUND"));
        a.call("c", "CREATE_TASK", title_payload("t"));
        CHECK_EQ(code_of(b.call("r", "RELEASE_TASK", id_payload(1))), std::string("CONFLICT"));   // not claimed
        CHECK_EQ(status_of(a.call("k", "CLAIM_TASK", id_payload(1))), std::string("OK"));
        CHECK_EQ(code_of(b.call("k", "CLAIM_TASK", id_payload(1))), std::string("CONFLICT"));     // already claimed
        CHECK_EQ(status_of(b.call("d", "COMPLETE_TASK", id_payload(1))), std::string("OK"));
        CHECK_EQ(code_of(a.call("d", "COMPLETE_TASK", id_payload(1))), std::string("CONFLICT"));  // already done
        CHECK_EQ(code_of(a.call("k", "CLAIM_TASK", id_payload(1))), std::string("CONFLICT"));     // done tasks stay done
        a.call("q", "QUIT");
        b.call("q", "QUIT");
    });

    testing::run("R-02", "graceful shutdown with 3 clients connected: all closed, logged, exit 0", [] {
        ServerProcess server;
        TestConn a(server.port());
        TestConn b(server.port());
        TestConn c(server.port());
        a.hello("Ana");
        b.hello("Ben");
        c.hello("Cara");
        server.process().send_signal(SIGTERM);
        CHECK(a.next_is_eof());
        CHECK(b.next_is_eof());
        CHECK(c.next_is_eof());
        CHECK_EQ(server.stop(), 0);
        CHECK(server.log_contains("sess-0001 closed by server shutdown"));
        CHECK(server.log_contains("sess-0002 closed by server shutdown"));
        CHECK(server.log_contains("sess-0003 closed by server shutdown"));
        CHECK(server.log_contains("Server shutdown complete"));
    });

    testing::run("R-03", "restart: new server on the same port accepts clients; tasks are not persisted", [] {
        int port = 0;
        {
            ServerProcess first;
            port = first.port();
            TestConn c(port);
            c.hello("Ana");
            c.call("t", "CREATE_TASK", title_payload("lost on restart"));
            CHECK_EQ(first.stop(), 0);
        }
        ServerProcess second(port, "_restart");
        TestConn c(port);
        CHECK_EQ(status_of(c.hello("Ana")), std::string("OK"));
        CHECK_EQ(list_count(c, "l"), 0);                       // documented limitation: memory only
        CHECK_EQ(status_of(c.call("t", "CREATE_TASK", title_payload("fresh"))), std::string("OK"));
        c.call("q", "QUIT");
    });

    testing::run("R-04", "log lines from concurrent sessions are never interleaved or lost", [] {
        ServerProcess server;
        parallel(4, [&](int i) {
            TestConn c(server.port());
            c.hello(name_for(i));
            for (int k = 0; k < 25; ++k) c.call("t", "CREATE_TASK", title_payload("log"));
            c.call("q", "QUIT");
        });
        server.wait_for_log("session released (active clients: 0)");
        const std::string log = server.process().output();
        std::size_t creates = 0;
        std::size_t pos = 0;
        while ((pos = log.find("CREATE_TASK success task_id=", pos)) != std::string::npos) { ++creates; ++pos; }
        CHECK_EQ(creates, 100u);
        // every line starts with a timestamp (no two writers shared a line)
        std::size_t start = 0;
        while (start < log.size()) {
            const std::size_t end = log.find('\n', start);
            const std::string line = log.substr(start, end - start);
            CHECK(line.size() > 21 && line[4] == '-' && line[10] == 'T' && line[19] == 'Z');
            if (end == std::string::npos) break;
            start = end + 1;
        }
    });

    return testing::summarize("test_concurrency");
}
