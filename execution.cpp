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
    /*
    TODO: Load trades from a CSV file.
    - Open the file and read its contents into a buffer.
    - Skip the header line (assumed to be present).
    - For each line:
        - Ignore blank lines.
        - Parse three fields: timestamp (int64), price (double), size (int64).
        - Throw an error if any field is malformed.
        - Store each trade in a vector.
    - Return the vector of trades.
    */


    //opens the file and assigns file to the new string that will hold the contents of the file
    //uses binary to keep same data types
    //uses ate to jump to the end of the file to get the size of the file
    
    //throws error if file cannot be opened
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    //finds cursor and uses it to gauge size of file
    auto size = file.tellg();
    //moves cursor back to the beginning of the file
    file.seekg(0, std::ios::beg);
    //creates container to hold the file contents called buffer
    std::string buffer;
    //sizes buffer( the string) to be the siz of the file
    buffer.resize(size);
    //copies the data from the file, directly into the buffer string 
    //which exists within memory for easy access
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file");
    }

    //creates trade vector and reserves space, specifically one byte per trade
    std::vector<Trade> trades;
    trades.reserve(size / 30); 

    //creates pointers for the start and the end of the buffer
    const char* ptr = buffer.data();
    const char* end = ptr + size;

    //while pointer to begining is less than pointer to end and current character isnt a new line
    //move the pointer forward by one
    while (ptr < end && *ptr != '\n') ++ptr;
    if (ptr < end) ++ptr; 


    //while pointer to begining is less than pointer to end
    while (ptr < end) {
        //checks for new lines or carriage returns and moves pointer forward by one if it finds them
        if (*ptr == '\n' || *ptr == '\r') {
            ++ptr;
            continue;
        }
        
        //creates a new trade object to hold the data for the current line
        Trade t;
        
        //gets the result from the from_chars function
        auto res = std::from_chars(ptr, end, t.ts_ms);
        //checks if successful, throws error if not
        //moves pointer forward to the end of the parsed number
        //checks for comma and moves pointer forward by one if it finds it
        if (res.ec != std::errc()) throw std::runtime_error("Malformed ts_ms");
        ptr = res.ptr;
        if (ptr < end && *ptr == ',') ++ptr;

        //same but for price
        res = std::from_chars(ptr, end, t.price);
        if (res.ec != std::errc()) throw std::runtime_error("Malformed price");
        ptr = res.ptr;
        if (ptr < end && *ptr == ',') ++ptr;

        //same but for size
        res = std::from_chars(ptr, end, t.size);
        if (res.ec != std::errc()) throw std::runtime_error("Malformed size");
        ptr = res.ptr;

        //adds the trade to the vector of trades
        trades.push_back(t);

        //moves pointer forward until it finds a new line
        //then moves it forward by one to get to the start of the next line
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end) ++ptr;
    }

    return trades;
    



}


    





void sort_trades_by_time(std::vector<Trade> &trades) {
        /*
        TODO: Sort trades by timestamp and compute prefix sums.
        - Sort the trades in ascending order of timestamp (ts_ms) using stable_sort.
        - For each trade, compute running totals:
            - current_pq: sum of price * size up to this trade.
            - current_q: sum of size up to this trade.
            - Store these as prefix_pq and prefix_q in each Trade struct.
        */
    // TODO: stable_sort by ts_ms ascending

    //runs the actual sorting algorithm by using the start, end, 
    //and a lambda function to compare time a, b, and sort them eventually
    std::stable_sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {return a.ts_ms < b.ts_ms;});

    //initalizes current p and pq to zero to then fill prefix values we added to trade struct
    double current_pq = 0.0;
    std::int64_t current_q = 0;

    //O(N)
    //for loop to create values for prefix q and pq for each trade
    //does this to save time later on when calculating vwap
    for (auto& t : trades) {
        current_pq += t.price * static_cast<double>(t.size);
        current_q += t.size;
        t.prefix_pq = current_pq;
        t.prefix_q = current_q;
    }
}






std::vector<double> rolling_vwap(const std::vector<Trade> &trades, std::int64_t window_ms) {
        /*
        TODO: Compute rolling VWAP (Volume Weighted Average Price) for each trade.
        - For each trade, consider a window of trades within [current_ts - window_ms, current_ts].
        - Use two pointers (left, i) to maintain the window efficiently.
        - Maintain running sums:
            - sum_pq: sum of price * size within the window.
            - sum_q: sum of size within the window.
        - For each trade:
            - Add current trade to sums.
            - Remove trades outside the window from sums.
            - If sum_q == 0, push NaN (no trades in window), else push sum_pq / sum_q.
        - Return a vector of VWAP values, one for each trade.
        */

    //creates the vector for the results of the rolling vwap values
    std::vector<double> result;
    //reserves the space for the results to be the same as the number of trades
    result.reserve(trades.size());

    //initializes the values for the sums and the left pointer
    double sum_pq = 0.0;
    std::int64_t sum_q = 0;
    std::size_t left = 0;

// this next code block works as follows:
    //it is made up of a for loop that has the one job of iterating through the trades vector
    //within the loop the first thing it does is add the current trade p and pq to the sums
    //then it checks if the current trade is outside the window by using a while loop
    //this loop checks to see if the left pointer is still within the window and if not
    //it removes the left trade from the sums and moves the left pointer up by one
    //then it moves on to adding the vwap to the result vector


    //**** likely that the while loop will run every iteration of the for loop after 
    //**** the window is crossed



    //O(N)
    //forloop that goes till i is the same value as the number of trades
    for (std::size_t i = 0; i < trades.size(); ++i) {
        //calculates the sum of price*size and size for current trade (i)
        sum_pq += trades[i].price * static_cast<double>(trades[i].size);
        sum_q += trades[i].size;

        //while loop to check if the left pointer is still within the window
        //if not it removes the left trade from the sums and moves the left ptr up
        while (left <= i && trades[left].ts_ms < trades[i].ts_ms - window_ms) {
            sum_pq -= trades[left].price * static_cast<double>(trades[left].size);
            sum_q -= trades[left].size;
            left++;
        }

        //puts Nan if there arent any trades/ no volume
        if (sum_q == 0) {
            result.push_back(NaN()); 
        } 
        //otherwise puts the vwap for the current trade
        else {
            result.push_back(sum_pq / static_cast<double>(sum_q));
        }
    }


    return result;

    // TODO:
    // - Use a two-pointer window:
    // - Maintain sums over window: sum_pq = Σ(price*size), sum_q = Σ(size)
    // - For each i, advance left pointer while trades[left].ts_ms <
    // - trades[i].ts_ms - window_ms Compute vwap = sum_pq / sum_q
    //return {};
}







double vwap_range(const std::vector<Trade> &trades, std::int64_t start_ts_ms, std::int64_t end_ts_ms) {
        /*
        TODO: Compute VWAP for trades in a given timestamp range [start_ts_ms, end_ts_ms].
        - Use lower_bound to find the first trade >= start_ts_ms.
        - Use upper_bound to find the first trade > end_ts_ms.
        - If no trades in range, return NaN.
        - Use prefix sums to efficiently compute:
            - total_pq: sum of price * size in range.
            - total_q: sum of size in range.
        - If total_q == 0, return NaN. Otherwise, return total_pq / total_q.
        */

    //this fetches the value for the first trade that occured right at the start time
    auto start_it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
        [](const Trade& t, std::int64_t ts) { return t.ts_ms < ts; });
 
   //this fetches the first trade/time AFTER the end time 
    auto end_it = std::upper_bound(start_it, trades.end(), end_ts_ms,
        [](std::int64_t ts, const Trade& t) { return ts < t.ts_ms; });

    //if there are no trades in the range return NaN
    if (start_it == end_it) {
        return NaN(); 
    }

    //sets last trade to the one right BEFORE the end time
    auto last_it = end_it - 1;

    //calculates total pq of trades up to the last trade in the range using prefix sums
    double total_pq = last_it->prefix_pq;
    //calculates total q of trades up to the last trade in the range using prefix sums
    std::int64_t total_q = last_it->prefix_q;

    //if the start trade isnt the beginning of the vector then we need to cut the total q and pq down
    //we do this by subtracting the total pq and q by the resepective prefix pq and qs of the start trade
    if (start_it != trades.begin()) {
        auto prev_it = start_it - 1;
        total_pq -= prev_it->prefix_pq;
        total_q -= prev_it->prefix_q;
    }

    //if there is no volume in the range, NAN
    if (total_q == 0) return NaN();
    //otherwise calculate vwap and return
    return total_pq / static_cast<double>(total_q);

    // TODO:
    // - iterate to compute VWAP over trades in inclusive range
    // - return NaN if none
    //return NaN();
}









/*
TODO: Find the arrival price at or after a given timestamp.
- Use lower_bound to find the first trade with ts_ms >= start_ts_ms.
- If found, return its price. Otherwise, return std::nullopt.
*/

std::optional<double> arrival_price(const std::vector<Trade> &trades, std::int64_t start_ts_ms) {

    //this finds the first trade with a timestamp greater than or equal to the start time
    auto it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
        [](const Trade& t, std::int64_t ts) {
            return t.ts_ms < ts;
        });

    //if such a trade exists return the price of that trade
    if (it != trades.end()) {
        return it->price;
    }

    //if no such trade exists return nullopt
    return std::nullopt;
    // TODO:
    // - find first trade with ts_ms >= start_ts_ms
    // - return price or nullopt
    //return std::nullopt;
}







/*
                                    TODO: Simulate the execution of a buy or sell order.
                                    - Initialize an ExecutionReport to store results.
                                    - If requested_qty <= 0, return empty report.
                                    - Find the first trade at or after start_ts_ms (arrival).
                                    - If no such trade, return empty report.
                                    - Set arrival price from the first eligible trade.
                                    - While there is quantity left to fill and trades available:
                                        - Take as much as possible from each trade (up to remaining_qty).
                                        - Record each fill (timestamp, price, qty).
                                        - Update filled_qty, notional, and end_ts_ms.
                                    - After filling:
                                        - Compute average fill price (weighted by qty).
                                        - Compute VWAP over the execution window.
                                        - Compute slippage vs. arrival price and VWAP (in basis points), direction depends on side (Buy/Sell).
                                    - Return the filled ExecutionReport.
                                    */
ExecutionReport simulate_execution(const std::vector<Trade> &trades, Side side, std::int64_t start_ts_ms, std::int64_t requested_qty) {

    //initializes the execution report with default values
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

    //if the requested quantity is zero or negative, return the empty report
    if (requested_qty <= 0) {
        return r;
    }

    //finds first trade that exists at or after the start time
    auto it = std::lower_bound(trades.begin(), trades.end(), start_ts_ms,
        [](const Trade& t, std::int64_t ts) { return t.ts_ms < ts; });

    //if no such trade exists, return the empty report
    if (it == trades.end()) {
        return r;
    }

    //sets arrival price equal to the price of the first trade found at start time given
    r.arrival_price = it->price;
    //initializes reminaing quantity to requestsed as nothing has been filled yet
    std::int64_t remaining_qty = requested_qty;

    //while there are still trades to fill against and we still have quantity to fill
    while (it != trades.end() && remaining_qty > 0) {
        //sets take qty to the amount of quantity traded
        std::int64_t take_qty = std::min(remaining_qty, it->size);
        
        //creates a fill order to represent the time of the trade, the price, and the quantity taken
        Fill f;
        f.ts_ms = it->ts_ms;
        f.price = it->price;
        f.qty = take_qty;
        //adds the fill order to the execution report's fills vector
        r.fills.push_back(f);

        //updates filled qty, notional, and end time of the execution report
        r.filled_qty += take_qty;
        r.notional += it->price * static_cast<double>(take_qty);
        r.end_ts_ms = it->ts_ms;
        
        //updates the remaining quantity to fill by subtracting the quantity we just took
        remaining_qty -= take_qty;
        ++it;
    }

    //if we filled any quantity, calculate the average fill price
    if (r.filled_qty > 0) {
        r.avg_fill_price = r.notional / static_cast<double>(r.filled_qty);
    }

    //calculates vwap over time using vwap range
    r.vwap_exec_window = vwap_range(trades, start_ts_ms, r.end_ts_ms);

    //calculates slippage vs arrival price and vwap in basis points, direction depends on side
    if (r.filled_qty > 0) {
        //checks if arrival price is not NaN before calculating slippage vs arrival price
        if (!std::isnan(r.arrival_price)) {
            if (side == Side::Buy) {
                r.slippage_vs_arrival_bps = ((r.avg_fill_price - r.arrival_price) / r.arrival_price) * 10000.0;
            } else {
                r.slippage_vs_arrival_bps = ((r.arrival_price - r.avg_fill_price) / r.arrival_price) * 10000.0;
            }
        }

        //checks if vwap is not NaN before calculating slippage vs vwap
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