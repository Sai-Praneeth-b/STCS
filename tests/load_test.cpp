// Phase 3 load test: starts a real server on a free local port, connects N client
// threads, each sends M mixed requests, and reports totals and response times.
// Usage: build/load_test [bin_dir] [clients] [requests_per_client]
// Runs only against a server it starts itself on 127.0.0.1 (no outside systems).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "tests/integration_support.hpp"

using namespace stcs;
using Clock = std::chrono::steady_clock;

int main(int argc, char* argv[]) {
    if (argc > 1) g_bin_dir = argv[1];
    const int clients = argc > 2 ? std::atoi(argv[2]) : 20;
    const int per_client = argc > 3 ? std::atoi(argv[3]) : 100;
    ::signal(SIGPIPE, SIG_IGN);
    ::alarm(120);

    ServerProcess server(0, "_load");
    std::atomic<long> ok{0}, rejected{0}, failed{0};
    std::atomic<int> finished{0};
    std::mutex m;
    std::vector<double> latencies_ms;

    const auto begin = Clock::now();
    std::vector<std::thread> threads;
    for (int i = 0; i < clients; ++i) {
        threads.emplace_back([&, i] {
            std::vector<double> mine;
            try {
                TestConn c(server.port());
                if (status_of(c.hello("Load" + std::to_string(i))) != "OK") { ++failed; ++finished; return; }
                for (int k = 0; k < per_client; ++k) {
                    const auto t0 = Clock::now();
                    Json r;
                    switch (k % 4) {
                        case 0: r = c.call("r", "CREATE_TASK", "{\"title\":\"load\"}"); break;
                        case 1: r = c.call("r", "LIST_TASKS"); break;
                        case 2: r = c.call("r", "CLAIM_TASK", "{\"task_id\":" + std::to_string(1 + (k * 7 + i) % 50) + "}"); break;
                        default: r = c.call("r", "COMPLETE_TASK", "{\"task_id\":" + std::to_string(1 + (k * 5 + i) % 50) + "}"); break;
                    }
                    mine.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                    // OK or a deliberate business rejection (CONFLICT / NOT_FOUND) both count as served
                    const std::string code = code_of(r);
                    if (status_of(r) == "OK") ++ok;
                    else if (code == "CONFLICT" || code == "NOT_FOUND") ++rejected;
                    else ++failed;
                }
                c.call("q", "QUIT");
            } catch (const std::exception& problem) {
                std::fprintf(stderr, "client %d: %s\n", i, problem.what());
                ++failed;
            }
            ++finished;
            std::lock_guard<std::mutex> lock(m);
            latencies_ms.insert(latencies_ms.end(), mine.begin(), mine.end());
        });
    }
    while (finished.load() < clients) server.process().drain_for(20);   // keep server stdout drained
    for (std::thread& t : threads) t.join();
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();

    std::sort(latencies_ms.begin(), latencies_ms.end());
    double sum = 0;
    for (double v : latencies_ms) sum += v;
    const double avg = latencies_ms.empty() ? 0 : sum / latencies_ms.size();
    const double p95 = latencies_ms.empty() ? 0 : latencies_ms[latencies_ms.size() * 95 / 100];
    const double max = latencies_ms.empty() ? 0 : latencies_ms.back();

    const int code = server.stop();
    std::printf("STCS Phase 3 load test (local, 127.0.0.1, own server)\n");
    std::printf("  clients:               %d simultaneous\n", clients);
    std::printf("  requests per client:   %d (+1 HELLO, +1 QUIT)\n", per_client);
    std::printf("  total requests:        %ld\n", ok.load() + rejected.load() + failed.load());
    std::printf("  duration:              %.2f s (%.0f requests/s)\n", seconds,
                seconds > 0 ? (ok + rejected + failed) / seconds : 0.0);
    std::printf("  successful (OK):       %ld\n", ok.load());
    std::printf("  rejected by design:    %ld (CONFLICT / NOT_FOUND, correct replies)\n", rejected.load());
    std::printf("  failed:                %ld (no/invalid reply or dropped connection)\n", failed.load());
    std::printf("  response time:         avg %.3f ms, p95 %.3f ms, max %.3f ms\n", avg, p95, max);
    std::printf("  server shutdown:       exit code %d\n", code);
    const bool pass = failed == 0 && code == 0;
    std::printf("%s\n", pass ? "LOAD TEST PASSED" : "LOAD TEST FAILED");
    return pass ? 0 : 1;
}
