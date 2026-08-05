// ============================================================
//  KVEngine YCSB-style Benchmark
//  Workloads A, B, C, D, F  (industry standard)
//  Metrics: ops/sec, avg latency, p99 latency per workload
// ============================================================

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <iomanip>
#include <sstream>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

// ── Config ────────────────────────────────────────────────
static constexpr int    NUM_CLIENTS     = 50;
static constexpr int    WARMUP_KEYS     = 10000;   // pre-loaded before each workload
static constexpr int    KEY_SPACE       = 100000;
static constexpr int    WORKLOAD_SEC    = 30;       // seconds per workload
static constexpr int    PORT            = 8080;
static const     string HOST            = "127.0.0.1";

// ── Workload definitions (YCSB standard) ─────────────────
struct WorkloadDef {
    string name;
    string description;
    int    readPct;    // percentage of reads
    int    writePct;   // percentage of writes  (insert/update)
    int    deletePct;  // percentage of deletes
    // readPct + writePct + deletePct == 100
};

static const vector<WorkloadDef> WORKLOADS = {
    { "A", "Update Heavy    (50R / 50W)",  50,  50,  0 },
    { "B", "Read Heavy      (95R /  5W)",  95,   5,  0 },
    { "C", "Read Only       (100R)",       100,   0,  0 },
    { "D", "Read Latest     (95R /  5W)",   95,   5,  0 },  // writes go to fresh keys
    { "F", "Read-Modify-Write (50R / 50W)", 50,  50,  0 },  // same key read before write
};

// ── Per-thread latency storage ────────────────────────────
struct alignas(64) ThreadStats {
    vector<long long> latencies_us;   // microseconds
    long long         ops    = 0;
    long long         errors = 0;
};

// ── TCP helpers ───────────────────────────────────────────
int connect_to_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, HOST.c_str(), &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Send a command, read response. Returns response string or "" on error.
string send_cmd(int fd, const string& cmd) {
    string msg = cmd + "\n";
    if (send(fd, msg.c_str(), msg.size(), 0) <= 0) return "";

    char buf[4096] = {};
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "";
    return string(buf, n);
}

// ── Key generators ────────────────────────────────────────
string make_key(int n)   { return "key"   + to_string(n); }
string make_value(int n) { return "value" + to_string(n); }

// ── Warmup: pre-load WARMUP_KEYS keys so reads have data ─
void warmup(int total_keys) {
    cout << "  Warming up (" << total_keys << " keys)... " << flush;
    int fd = connect_to_server();
    if (fd < 0) { cout << "FAILED (is server running?)\n"; exit(1); }

    for (int i = 0; i < total_keys; i++) {
        string cmd = "SET " + make_key(i) + " " + make_value(i);
        send_cmd(fd, cmd);
    }
    close(fd);
    cout << "done\n";
}

// ── Worker thread ─────────────────────────────────────────
void worker(
    int              thread_id,
    const WorkloadDef& wl,
    atomic<bool>&    stop,
    ThreadStats&     stats,
    bool             latest_keys   // workload D: write to fresh key range
) {
    int fd = connect_to_server();
    if (fd < 0) { stats.errors++; return; }

    mt19937 rng(random_device{}() ^ (thread_id * 2654435761u));
    uniform_int_distribution<int> key_dist(0, KEY_SPACE - 1);
    uniform_int_distribution<int> pct_dist(0, 99);

    // For workload D: fresh keys start above the warmup range
    static atomic<int> latest_counter(WARMUP_KEYS);

    stats.latencies_us.reserve(50000);

    while (!stop.load(memory_order_relaxed)) {
        int p   = pct_dist(rng);
        int key = key_dist(rng);

        string cmd;

        if (p < wl.readPct) {
            // READ
            cmd = "GET " + make_key(key);
        } else if (p < wl.readPct + wl.writePct) {
            // WRITE
            if (latest_keys) {
                int fresh = latest_counter.fetch_add(1, memory_order_relaxed) % KEY_SPACE;
                cmd = "SET " + make_key(fresh) + " " + make_value(fresh);
            } else if (wl.name == "F") {
                // Read-Modify-Write: read first, then write same key
                send_cmd(fd, "GET " + make_key(key));
                cmd = "SET " + make_key(key) + " " + make_value(key);
            } else {
                cmd = "SET " + make_key(key) + " " + make_value(key);
            }
        } else {
            // DELETE
            cmd = "DEL " + make_key(key);
        }

        auto t0  = steady_clock::now();
        string r = send_cmd(fd, cmd);
        auto t1  = steady_clock::now();

        if (r.empty()) {
            stats.errors++;
            // reconnect
            close(fd);
            fd = connect_to_server();
            if (fd < 0) break;
            continue;
        }

        long long us = duration_cast<microseconds>(t1 - t0).count();
        stats.latencies_us.push_back(us);
        stats.ops++;
    }

    close(fd);
}

// ── Stats aggregation ─────────────────────────────────────
struct WorkloadResult {
    string   workload;
    string   description;
    double   ops_per_sec;
    double   avg_latency_us;
    double   p50_latency_us;
    double   p99_latency_us;
    double   p999_latency_us;
    long long total_ops;
    long long total_errors;
};

WorkloadResult run_workload(const WorkloadDef& wl) {
    cout << "\n  Running Workload " << wl.name
         << " — " << wl.description << "\n";

    // Warmup before each workload so reads always have data
    warmup(WARMUP_KEYS);

    vector<ThreadStats> stats(NUM_CLIENTS);
    atomic<bool> stop(false);
    vector<thread> threads;

    bool latest = (wl.name == "D");

    auto t_start = steady_clock::now();

    for (int i = 0; i < NUM_CLIENTS; i++) {
        threads.emplace_back(worker, i, cref(wl), ref(stop),
                             ref(stats[i]), latest);
    }

    this_thread::sleep_for(seconds(WORKLOAD_SEC));
    stop = true;

    for (auto& t : threads) t.join();

    auto t_end = steady_clock::now();
    double elapsed = duration<double>(t_end - t_start).count();

    // Merge all latencies
    vector<long long> all;
    long long total_ops    = 0;
    long long total_errors = 0;

    for (auto& s : stats) {
        all.insert(all.end(), s.latencies_us.begin(), s.latencies_us.end());
        total_ops    += s.ops;
        total_errors += s.errors;
    }

    sort(all.begin(), all.end());

    double avg = all.empty() ? 0.0
        : (double)accumulate(all.begin(), all.end(), 0LL) / all.size();

    auto pct = [&](double p) -> double {
        if (all.empty()) return 0.0;
        size_t idx = (size_t)(p / 100.0 * all.size());
        if (idx >= all.size()) idx = all.size() - 1;
        return (double)all[idx];
    };

    return {
        wl.name,
        wl.description,
        total_ops / elapsed,
        avg,
        pct(50),
        pct(99),
        pct(99.9),
        total_ops,
        total_errors
    };
}

// ── Print results table ───────────────────────────────────
void print_results(const vector<WorkloadResult>& results) {
    cout << "\n";
    cout << "╔══════════════════════════════════════════════════════════════════════════════════╗\n";
    cout << "║                          KVEngine YCSB Benchmark Results                        ║\n";
    cout << "╠══════╦════════════════════════════╦══════════╦═════════╦═════════╦══════════════╣\n";
    cout << "║  WL  ║  Description               ║  ops/sec ║ avg(μs) ║ p99(μs) ║ p99.9(μs)   ║\n";
    cout << "╠══════╬════════════════════════════╬══════════╬═════════╬═════════╬══════════════╣\n";

    for (auto& r : results) {
        cout << "║  " << left << setw(4) << r.workload
             << "║  " << left << setw(26) << r.description
             << "║  " << right << setw(7) << fixed << setprecision(0) << r.ops_per_sec << " "
             << "║  " << right << setw(6) << fixed << setprecision(1) << r.avg_latency_us << " "
             << "║  " << right << setw(6) << fixed << setprecision(1) << r.p99_latency_us << " "
             << "║  " << right << setw(10) << fixed << setprecision(1) << r.p999_latency_us << " "
             << "║\n";
    }

    cout << "╚══════╩════════════════════════════╩══════════╩═════════╩═════════╩══════════════╝\n";

    cout << "\n  Details:\n";
    for (auto& r : results) {
        cout << "  Workload " << r.workload
             << " — total ops: " << r.total_ops
             << "  errors: "     << r.total_errors
             << "  p50: "        << fixed << setprecision(1) << r.p50_latency_us << "μs"
             << "\n";
    }

    // Resume-ready summary line
    cout << "\n  ── Resume line ──────────────────────────────────────────────────────\n";
    if (!results.empty()) {
        double best_ops = 0;
        for (auto& r : results) best_ops = max(best_ops, r.ops_per_sec);
        double wlA_ops = 0, wlB_ops = 0, wlC_ops = 0;
        double wlA_p99 = 0, wlB_p99 = 0;
        for (auto& r : results) {
            if (r.workload == "A") { wlA_ops = r.ops_per_sec; wlA_p99 = r.p99_latency_us; }
            if (r.workload == "B") { wlB_ops = r.ops_per_sec; wlB_p99 = r.p99_latency_us; }
            if (r.workload == "C")   wlC_ops = r.ops_per_sec;
        }
        cout << "  Benchmarked at "
             << (int)wlA_ops << " ops/sec (Workload A, 50R/50W), "
             << (int)wlB_ops << " ops/sec (Workload B, 95R/5W), "
             << (int)wlC_ops << " ops/sec (Workload C, read-only)\n"
             << "  across " << NUM_CLIENTS << " concurrent clients "
             << "with p99 latency " << fixed << setprecision(2) << (wlA_p99 / 1000.0) << "ms (A) / "
             << fixed << setprecision(2) << (wlB_p99 / 1000.0) << "ms (B)\n";
    }
    cout << "  ─────────────────────────────────────────────────────────────────────\n";
}

// ── Main ──────────────────────────────────────────────────
int main() {
    signal(SIGPIPE, SIG_IGN);

    cout << "╔══════════════════════════════════════════════════╗\n";
    cout << "║         KVEngine YCSB-style Benchmark            ║\n";
    cout << "╠══════════════════════════════════════════════════╣\n";
    cout << "║  Clients   : " << left << setw(36) << NUM_CLIENTS   << "║\n";
    cout << "║  Key space : " << left << setw(36) << KEY_SPACE     << "║\n";
    cout << "║  Duration  : " << left << setw(34) << (to_string(WORKLOAD_SEC) + "s / workload") << "║\n";
    cout << "║  Workloads : A, B, C, D, F                       ║\n";
    cout << "╚══════════════════════════════════════════════════╝\n";

    // Quick connectivity check
    {
        int fd = connect_to_server();
        if (fd < 0) {
            cerr << "\nERROR: Cannot connect to server at "
                 << HOST << ":" << PORT << "\n"
                 << "       Start the server first:  ./kvserver\n\n";
            return 1;
        }
        close(fd);
        cout << "\n  Server reachable at " << HOST << ":" << PORT << "\n";
    }

    vector<WorkloadResult> results;
    for (auto& wl : WORKLOADS) {
        results.push_back(run_workload(wl));
    }

    print_results(results);
    return 0;
}