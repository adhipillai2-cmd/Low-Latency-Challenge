#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
 
namespace execsim {

struct Trade {
    std::int64_t ts_ms; // epoch milliseconds
    double price;
    std::int64_t size; // shares

    double prefix_pq = 0.0;
    std::int64_t prefix_q = 0;
};

enum class Side { Buy, Sell };

struct Fill {
    std::int64_t ts_ms;
    double price;
    std::int64_t qty;
};

struct ExecutionReport {
    Side side;
    std::int64_t start_ts_ms;
    std::int64_t
        end_ts_ms; // last fill time (or last trade time if not fully filled)
    std::int64_t requested_qty;
    std::int64_t filled_qty;

    double avg_fill_price; // weighted by qty
    double notional;       // sum(price * qty)
    double arrival_price; // first trade price at/after start time (NaN if none)

    double vwap_exec_window;        // VWAP of all prints between start and end
                                    // (inclusive) (NaN if empty)
    double slippage_vs_arrival_bps; // (avg_fill - arrival) / arrival * 1e4 for
                                    // Buy; reversed for Sell
    double slippage_vs_vwap_bps;    // (avg_fill - vwap) / vwap * 1e4 for Buy;
                                    // reversed for Sell

    std::vector<Fill> fills;
};

// --- Parsing ---
// Parse trades from a CSV file with header: ts_ms,price,size
// Requirements: ignore blank lines, allow spaces, validate fields.
std::vector<Trade> load_trades_csv(const std::string &path);

// Ensure trades are strictly non-decreasing by timestamp
void sort_trades_by_time(std::vector<Trade> &trades);

// --- Analytics ---
// Rolling VWAP at each trade i over [trades[i].ts_ms - window_ms,
// trades[i].ts_ms] inclusive. Return vector same length as trades; NaN where
// window is empty.
std::vector<double> rolling_vwap(const std::vector<Trade> &trades,
                                 std::int64_t window_ms);

// VWAP of prints in [start_ts_ms, end_ts_ms] inclusive.
// Return NaN if no prints in range.
double vwap_range(const std::vector<Trade> &trades, std::int64_t start_ts_ms,
                  std::int64_t end_ts_ms);

// First trade price with ts_ms >= start_ts_ms. Return nullopt if none.
std::optional<double> arrival_price(const std::vector<Trade> &trades,
                                    std::int64_t start_ts_ms);

// --- Execution simulator ---
// Simulate executing requested_qty starting from start_ts_ms.
// Fill against prints in chronological order with ts_ms >= start_ts_ms.
// For Buy: you pay trade price; for Sell: you receive trade price.
ExecutionReport simulate_execution(const std::vector<Trade> &trades, Side side,
                                   std::int64_t start_ts_ms,
                                   std::int64_t requested_qty);

} // namespace execsim