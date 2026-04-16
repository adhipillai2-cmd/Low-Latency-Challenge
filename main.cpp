// DO NOT EDIT
// SSMIF MFM High-Performance Programming Challenge

#include "execution.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using execsim::Side;
using execsim::Trade;

static volatile std::uint64_t g_sink = 0;
static void sink_u64(std::uint64_t x) { g_sink ^= x; }

static double percentile(std::vector<double> v, double p) {
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = p * (double)(v.size() - 1);
    const std::size_t i = (std::size_t)idx;
    const double frac = idx - (double)i;
    if (i + 1 < v.size())
        return v[i] * (1.0 - frac) + v[i + 1] * frac;
    return v[i];
}

static std::vector<Trade> synthetic_tape(std::size_t N,
                                         std::uint64_t seed = 42) {
    std::vector<Trade> t;
    t.reserve(N);

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.02);
    std::bernoulli_distribution jump_event(0.0005);
    std::normal_distribution<double> jump_size(0.0, 0.25);
    std::uniform_int_distribution<int> size_dist(10, 500);
    std::uniform_int_distribution<int> dt_dist(1, 3);

    std::int64_t ts = 1700000000000;
    double px = 100.00;

    for (std::size_t i = 0; i < N; ++i) {
        ts += dt_dist(rng);
        px += 0.00005;
        px += noise(rng);
        if (jump_event(rng))
            px += jump_size(rng);
        if (px < 1.0)
            px = 1.0;

        const std::int64_t sz = (std::int64_t)size_dist(rng);
        t.push_back(Trade{ts, px, sz});
    }
    return t;
}

struct Order {
    Side side;
    std::int64_t start_ts_ms;
    std::int64_t qty;
};

static std::vector<Order> make_orders(const std::vector<Trade> &trades,
                                      std::size_t n_orders,
                                      std::uint64_t seed = 1234567) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> start_idx_dist(0, trades.size() -
                                                                     1);
    std::uniform_int_distribution<int> qty_dist(50, 5000);
    std::bernoulli_distribution side_dist(0.5);

    std::vector<Order> orders;
    orders.reserve(n_orders);

    for (std::size_t i = 0; i < n_orders; ++i) {
        std::size_t idx = start_idx_dist(rng);
        Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        std::int64_t qty = qty_dist(rng);
        orders.push_back(Order{side, trades[idx].ts_ms, qty});
    }
    return orders;
}

static void print_exec_report(const execsim::ExecutionReport &r) {
    const char *side_str = (r.side == Side::Buy) ? "BUY " : "SELL";
    std::cout << side_str << " Execution Report\n";

    std::cout << "  qty:          " << r.filled_qty << " / " << r.requested_qty
              << " (" << std::fixed << std::setprecision(1)
              << (100.0 * (r.requested_qty
                               ? (double)r.filled_qty / (double)r.requested_qty
                               : 0.0))
              << "%)\n";

    std::cout << std::setprecision(4);
    std::cout << "  avg_fill:     " << r.avg_fill_price << "\n";
    std::cout << "  notional:     " << std::setprecision(2) << r.notional
              << "\n";
    std::cout << std::setprecision(4);

    std::cout << "  arrival_px:   " << r.arrival_price << "\n";
    std::cout << "  vwap_window:  " << r.vwap_exec_window << "\n";

    std::cout << std::setprecision(3);
    std::cout << "  slip(arr):    " << r.slippage_vs_arrival_bps << " bps\n";
    std::cout << "  slip(vwap):   " << r.slippage_vs_vwap_bps << " bps\n";

    std::cout << "  fills:        " << r.fills.size() << "\n";
    std::cout << "  exec_window:  [" << r.start_ts_ms << " .. " << r.end_ts_ms
              << "]"
              << "  (" << (r.end_ts_ms - r.start_ts_ms) << " ms)\n";
}

static void usage(const char *prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog
        << " [--csv <path>] [--trades N] [--orders M] [--sample_every K]\n"
        << "         [--demo_qty Q] [--start_idx I]\n\n"
        << "Defaults:\n"
        << "  --csv           (none; generate in-memory)\n"
        << "  --trades        1000000\n"
        << "  --orders        1000000\n"
        << "  --sample_every  1000\n"
        << "  --demo_qty      50000\n"
        << "  --start_idx     250000\n";
}

int main(int argc, char **argv) {
    using clock = std::chrono::steady_clock;

    std::string csv_path;
    std::size_t n_trades = 1000000;
    std::size_t n_orders = 1000000;
    std::size_t sample_every = 1000;
    std::int64_t demo_qty = 50000;
    std::size_t start_idx = 250000;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char *flag) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                usage(argv[0]);
                std::exit(2);
            }
            return std::string(argv[++i]);
        };

        if (a == "--csv") {
            csv_path = need("--csv");
        } else if (a == "--trades") {
            n_trades = (std::size_t)std::stoull(need("--trades"));
        } else if (a == "--orders") {
            n_orders = (std::size_t)std::stoull(need("--orders"));
        } else if (a == "--sample_every") {
            sample_every = (std::size_t)std::stoull(need("--sample_every"));
            if (sample_every == 0)
                sample_every = 1;
        } else if (a == "--demo_qty") {
            demo_qty = (std::int64_t)std::stoll(need("--demo_qty"));
        } else if (a == "--start_idx") {
            start_idx = (std::size_t)std::stoull(need("--start_idx"));
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown flag: " << a << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    // ------------------ Load or generate tape ------------------
    std::vector<Trade> trades;
    double tape_ms = 0.0;

    if (!csv_path.empty()) {
        std::cout << "Loading tape from CSV: " << csv_path << "\n";
        auto t0 = clock::now();
        trades = execsim::load_trades_csv(csv_path);
        execsim::sort_trades_by_time(trades);
        auto t1 = clock::now();
        tape_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    } else {
        std::cout << "Generating tape...\n";
        auto t0 = clock::now();
        trades = synthetic_tape(n_trades);
        execsim::sort_trades_by_time(trades);
        auto t1 = clock::now();
        tape_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    if (trades.empty()) {
        std::cerr << "Error: tape is empty.\n";
        return 1;
    }
    if (start_idx >= trades.size())
        start_idx = trades.size() / 4;

    // Sanity checks
    for (std::size_t i = 1; i < trades.size(); ++i) {
        assert(trades[i].ts_ms >= trades[i - 1].ts_ms);
        assert(trades[i].price > 0.0);
        assert(trades[i].size > 0);
    }

    std::cout << "Tape: " << trades.size() << " trades (setup " << std::fixed
              << std::setprecision(2) << tape_ms << " ms)\n\n";

    // ------------------ Demo execution report ------------------
    const std::int64_t start_ts = trades[start_idx].ts_ms;
    auto buy_demo =
        execsim::simulate_execution(trades, Side::Buy, start_ts, demo_qty);
    auto sell_demo =
        execsim::simulate_execution(trades, Side::Sell, start_ts, demo_qty);

    print_exec_report(buy_demo);
    std::cout << "\n";
    print_exec_report(sell_demo);
    std::cout << "\n";

    // ------------------ Benchmark orders ------------------
    std::cout << "Generating " << n_orders << " orders...\n";
    auto orders = make_orders(trades, n_orders);

    // Warm-up
    for (int i = 0; i < 5000 && (std::size_t)i < orders.size(); ++i) {
        const auto &o = orders[(std::size_t)i];
        auto r =
            execsim::simulate_execution(trades, o.side, o.start_ts_ms, o.qty);
        sink_u64((std::uint64_t)r.filled_qty);
    }

    std::vector<double> sample_us;
    sample_us.reserve(n_orders / sample_every + 8);

    std::uint64_t total_filled = 0;
    long double total_notional = 0.0L;

    auto b0 = clock::now();

    for (std::size_t i = 0; i < n_orders; ++i) {
        const auto &o = orders[i];

        if (i % sample_every == 0) {
            auto s0 = clock::now();
            auto r = execsim::simulate_execution(trades, o.side, o.start_ts_ms,
                                                 o.qty);
            auto s1 = clock::now();
            sample_us.push_back(
                std::chrono::duration<double, std::micro>(s1 - s0).count());

            total_filled += (std::uint64_t)r.filled_qty;
            total_notional += (long double)r.notional;
        } else {
            auto r = execsim::simulate_execution(trades, o.side, o.start_ts_ms,
                                                 o.qty);
            total_filled += (std::uint64_t)r.filled_qty;
            total_notional += (long double)r.notional;
        }
    }

    auto b1 = clock::now();
    const double sec = std::chrono::duration<double>(b1 - b0).count();

    sink_u64(total_filled);
    sink_u64((std::uint64_t)total_notional);

    const double ops = (sec > 0.0) ? (double)n_orders / sec : 0.0;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Benchmark:\n";
    std::cout << "  tape:         " << trades.size() << " trades\n";
    std::cout << "  orders:       " << n_orders << "\n";
    std::cout << "  elapsed:      " << sec * 1000.0 << " ms\n";
    std::cout << "  throughput:   " << ops << " orders/sec\n";
    std::cout << "  avg latency:  " << (sec * 1e6) / (double)n_orders
              << " us/order\n";

    if (!sample_us.empty()) {
        std::cout << "  latency samp: " << sample_us.size() << " (1/"
                  << sample_every << ")\n";
        std::cout << "  p50 sample:   " << percentile(sample_us, 0.50)
                  << " us\n";
        std::cout << "  p95 sample:   " << percentile(sample_us, 0.95)
                  << " us\n";
        std::cout << "  p99 sample:   " << percentile(sample_us, 0.99)
                  << " us\n";
    }

    std::cout << "  total filled: " << total_filled << " shares\n";
    std::cout << "  total notnl:  " << (double)total_notional << "\n";

    std::cout << "\nAll checks passed.\n";
    return 0;
}