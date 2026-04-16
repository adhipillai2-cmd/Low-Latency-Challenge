#include "execution.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <charconv>

namespace execsim {

static double NaN() { return std::numeric_limits<double>::quiet_NaN(); }

std::vector<Trade> load_trades_csv(const std::string &path) {

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string buffer;
    buffer.resize(size);
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file");
    }


    std::vector<Trade> trades;
    trades.reserve(size / 30); 

    const char* ptr = buffer.data();
    const char* end = ptr + size;

    while (ptr < end && *ptr != '\n') ++ptr;
    if (ptr < end) ++ptr; 

    while (ptr < end) {
        if (*ptr == '\n' || *ptr == '\r') {
            ++ptr;
            continue;
        }

        Trade t;
        
        auto res = std::from_chars(ptr, end, t.ts_ms);
        if (res.ec != std::errc()) throw std::runtime_error("Malformed ts_ms");
        ptr = res.ptr;
        if (ptr < end && *ptr == ',') ++ptr;

        res = std::from_chars(ptr, end, t.price);
        if (res.ec != std::errc()) throw std::runtime_error("Malformed price");
        ptr = res.ptr;
        if (ptr < end && *ptr == ',') ++ptr;

        res = std::from_chars(ptr, end, t.size);
        if (res.ec != std::errc()) throw std::runtime_error("Malformed size");
        ptr = res.ptr;

        trades.push_back(t);

        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr;
    }

    return trades;
    



}


    





void sort_trades_by_time(std::vector<Trade> &trades) {
    // TODO: stable_sort by ts_ms ascending
    std::stable_sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {
        return a.ts_ms < b.ts_ms;
    });

    double current_pq = 0.0;
    std::int64_t current_q = 0;

    for (auto& t : trades) {
        current_pq += t.price * static_cast<double>(t.size);
        current_q += t.size;
        t.prefix_pq = current_pq;
        t.prefix_q = current_q;
    }
}






std::vector<double> rolling_vwap(const std::vector<Trade> &trades, std::int64_t window_ms) {
    std::vector<double> result;
    result.reserve(trades.size());

    double sum_pq = 0.0;
    std::int64_t sum_q = 0;
    std::size_t left = 0;

    for (std::size_t i = 0; i < trades.size(); ++i) {
        sum_pq += trades[i].price * static_cast<double>(trades[i].size);
        sum_q += trades[i].size;

        while (left <= i && trades[left].ts_ms < trades[i].ts_ms - window_ms) {
            sum_pq -= trades[left].price * static_cast<double>(trades[left].size);
            sum_q -= trades[left].size;
            left++;
        }

        if (sum_q == 0) {
            result.push_back(NaN()); 
        } else {
            result.push_back(sum_pq / static_cast<double>(sum_q));
        }
    }

    return result;
}







double vwap_range(const std::vector<Trade> &trades, std::int64_t start_ts_ms, std::int64_t end_ts_ms) {
    auto start_it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
        [](const Trade& t, std::int64_t ts) { return t.ts_ms < ts; });

    auto end_it = std::upper_bound(start_it, trades.end(), end_ts_ms,
        [](std::int64_t ts, const Trade& t) { return ts < t.ts_ms; });

    if (start_it == end_it) {
        return NaN(); 
    }

    auto last_it = end_it - 1;
    double total_pq = last_it->prefix_pq;
    std::int64_t total_q = last_it->prefix_q;


    if (start_it != trades.begin()) {
        auto prev_it = start_it - 1;
        total_pq -= prev_it->prefix_pq;
        total_q -= prev_it->prefix_q;
    }

    if (total_q == 0) return NaN();
    return total_pq / static_cast<double>(total_q);

}











std::optional<double> arrival_price(const std::vector<Trade> &trades,
                                    std::int64_t start_ts_ms) {

    auto it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
        [](const Trade& t, std::int64_t ts) {
            return t.ts_ms < ts;
        });

    if (it != trades.end()) {
        return it->price;
    }

    return std::nullopt;

}

ExecutionReport simulate_execution(const std::vector<Trade> &trades, Side side,
                                   std::int64_t start_ts_ms,
                                   std::int64_t requested_qty) {

ExecutionReport r{};
r.side = side;
r.start_ts_ms = start_ts_ms;
r.end_ts_ms = start_ts_ms;
r.requested_qty = requested_qty;
r.filled_qty = 0;
r.avg_fill_price = NaN();
r.notional = 0.0;
r.arrival_price = NaN();
r.vwap_exec_window = NaN();
r.slippage_vs_arrival_bps = NaN();
r.slippage_vs_vwap_bps = NaN();

if (requested_qty <= 0) {
    return r;
}

auto it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
    [](const Trade& t, std::int64_t ts) { return t.ts_ms < ts; });

if (it == trades.end()) {
    return r;
}

r.arrival_price = it->price;
std::int64_t remaining_qty = requested_qty;

while (it != trades.end() && remaining_qty > 0) {
    std::int64_t take_qty = std::min(remaining_qty, it->size);
    
    Fill f;
    f.ts_ms = it->ts_ms;
    f.price = it->price;
    f.qty = take_qty;
    r.fills.push_back(f);

    r.filled_qty += take_qty;
    r.notional += it->price * static_cast<double>(take_qty);
    r.end_ts_ms = it->ts_ms;
    
    remaining_qty -= take_qty;
    ++it;
}

if (r.filled_qty > 0) {
    r.avg_fill_price = r.notional / static_cast<double>(r.filled_qty);
}

r.vwap_exec_window = vwap_range(trades, start_ts_ms, r.end_ts_ms);

if (r.filled_qty > 0) {
    if (!std::isnan(r.arrival_price)) {
        if (side == Side::Buy) {
            r.slippage_vs_arrival_bps = ((r.avg_fill_price - r.arrival_price) / r.arrival_price) * 10000.0;
        } else {
            r.slippage_vs_arrival_bps = ((r.arrival_price - r.avg_fill_price) / r.arrival_price) * 10000.0;
        }
    }

    if (!std::isnan(r.vwap_exec_window)) {
        if (side == Side::Buy) {
            r.slippage_vs_vwap_bps = ((r.avg_fill_price - r.vwap_exec_window) / r.vwap_exec_window) * 10000.0;
        } else {
            r.slippage_vs_vwap_bps = ((r.vwap_exec_window - r.avg_fill_price) / r.vwap_exec_window) * 10000.0;
        }
    }
}

return r;
}

} // namespace execsim