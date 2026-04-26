#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include <sstream>
#include <fstream>

enum class Signal { BUY, SELL, HOLD };

struct Bar {
    std::string date;
    double open, high, low, close;
    long volume;
};

struct BacktestConfig {
    double initial_cash = 10000.0;
    double commission   = 0.0;
};

struct Trade {
    std::string date;
    Signal side;
    double price;
    int shares;
    double pnl;
    double return_pct;  // stored as percentage (e.g. 32.15)
};

using PriceData = std::vector<Bar>;

struct Context {
    const std::vector<Bar>& data;
    size_t current_index;

    double close()  const { return data[current_index].close; }
    double open()   const { return data[current_index].open; }
    double high()   const { return data[current_index].high; }
    double low()    const { return data[current_index].low; }
    double close_ago(size_t n) const { return data[current_index - n].close; }
};

class Strategy {
public:
    virtual Signal next(const Context& ctx) const = 0;
    virtual size_t warmup_bars() const { return 0; }
    virtual ~Strategy() = default;
};

class BacktestResult {
public:
    std::vector<Trade> trades;
    std::vector<double> equity_curve;
    std::vector<bool>   in_position;   // true on bars where shares > 0
    double initial_cash;
    int trading_days;
    std::vector<double> bench_returns;
    double first_close;
    double last_close;
    double benchmark_first_close;

    std::vector<double> daily_returns() const;
    double daily_returns_mean(const std::vector<double>& d) const;

    double total_return_pct()      const;
    double annualized_return_pct() const;
    double sharpe_ratio()          const;
    double sortino_ratio()         const;
    double calmar_ratio()          const;
    double max_drawdown_pct()      const;
    double avg_drawdown_pct()      const;
    double volatility_ann_pct()    const;
    double win_rate_pct()          const;
    int    total_trades()          const;
    double best_trade_pct()        const;
    double worst_trade_pct()       const;
    double avg_trade_pct()         const;
    double buyhold_return_pct()    const;
    double alpha_pct()             const;
    double beta()                  const;
};

// ── free functions ────────────────────────────────────────────────────────────

bool crossover(double prev, double curr, double threshold) {
    return prev < threshold && curr >= threshold;
}

bool crossover(double prev_a, double prev_b, double curr_a, double curr_b) {
    return prev_a < prev_b && curr_a >= curr_b;
}

PriceData load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to open: " + path);

    PriceData result;
    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string date, close_s, high_s, low_s, open_s, vol_s;
        std::getline(ss, date,    ',');
        std::getline(ss, close_s, ',');
        std::getline(ss, high_s,  ',');
        std::getline(ss, low_s,   ',');
        std::getline(ss, open_s,  ',');
        std::getline(ss, vol_s);

        Bar row;
        row.date   = date;
        row.close  = std::stod(close_s);
        row.high   = std::stod(high_s);
        row.low    = std::stod(low_s);
        row.open   = std::stod(open_s);
        row.volume = static_cast<long>(std::stod(vol_s));
        result.push_back(row);
    }
    return result;
}

BacktestResult run_backtest(const PriceData& data,
                            const Strategy&  strategy,
                            const BacktestConfig& config) {
    BacktestResult result;
    if (data.empty()) return result;
    result.initial_cash  = config.initial_cash;
    result.trading_days  = static_cast<int>(data.size());
    result.first_close   = data.front().close;
    result.last_close    = data.back().close;
    const size_t warmup_idx = std::min(strategy.warmup_bars(), data.size() - 1);
    result.benchmark_first_close = data[warmup_idx].close;

    double cash   = config.initial_cash;
    double shares = 0;
    Signal pending_order = Signal::HOLD;

    for (size_t idx = 0; idx < data.size(); idx++) {
        // Execute any order queued by the previous bar's signal at today's open.
        if (pending_order == Signal::BUY &&
            shares == 0 &&
            cash >= data[idx].open * (1 + config.commission)) {
            double ep = data[idx].open;
            Trade t;
            shares     = std::trunc(cash / ep);
            t.date     = data[idx].date;
            t.side     = Signal::BUY;
            t.price    = ep;
            t.shares   = static_cast<int>(shares);
            t.pnl      = 0;
            t.return_pct = 0;
            cash -= shares * ep * (1 + config.commission);
            result.trades.push_back(t);
        } else if (pending_order == Signal::SELL && shares > 0) {
            double ep         = data[idx].open;
            double last_price = result.trades.back().price;
            Trade t;
            t.date       = data[idx].date;
            t.side       = Signal::SELL;
            t.price      = ep;
            t.shares     = static_cast<int>(shares);
            t.pnl        = (ep - last_price) * shares;
            t.return_pct = ((ep - last_price) / last_price) * 100.0;
            cash += shares * ep * (1 - config.commission);
            result.trades.push_back(t);
            shares = 0;
        }
        pending_order = Signal::HOLD;

        Context ctx{data, idx};
        Signal signal = strategy.next(ctx);
        if (idx + 1 < data.size()) {
            if (signal == Signal::BUY && shares == 0) pending_order = Signal::BUY;
            if (signal == Signal::SELL && shares > 0) pending_order = Signal::SELL;
        }

        double pv = cash + shares * ctx.close();
        result.equity_curve.push_back(pv);
        result.in_position.push_back(shares > 0);
    }

    // force-close last open position
    if (shares > 0) {
        double ep         = data.back().open;
        double last_price = result.trades.back().price;
        Trade t;
        t.date       = data.back().date;
        t.side       = Signal::SELL;
        t.price      = ep;
        t.shares     = static_cast<int>(shares);
        t.pnl        = (ep - last_price) * shares;
        t.return_pct = ((ep - last_price) / last_price) * 100.0;
        cash += shares * ep * (1 - config.commission);
        result.trades.push_back(t);
        result.equity_curve.back() = cash;
        result.in_position.back()  = false;
        shares = 0;
    }

    // benchmark daily returns
    for (size_t i = 1; i < data.size(); i++)
        result.bench_returns.push_back(
            (data[i].close - data[i-1].close) / data[i-1].close);

    return result;
}

// ── metric implementations ────────────────────────────────────────────────────

namespace {

double geometric_mean_returns(const std::vector<double>& returns, bool include_initial_zero = false) {
    if (returns.empty() && !include_initial_zero) return 0.0;
    double log_sum = 0.0;
    for (double r : returns) {
        if (1.0 + r <= 0.0) return 0.0;
        log_sum += std::log(1.0 + r);
    }
    const double n = static_cast<double>(returns.size() + (include_initial_zero ? 1 : 0));
    if (n <= 0.0) return 0.0;
    return std::exp(log_sum / n) - 1.0;
}

double sample_stdev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    const double ss = std::accumulate(values.begin(), values.end(), 0.0,
        [mean](double s, double v) {
            const double diff = v - mean;
            return s + diff * diff;
        });
    return std::sqrt(ss / static_cast<double>(values.size() - 1));
}

} // namespace

std::vector<double> BacktestResult::daily_returns() const {
    std::vector<double> r;
    for (size_t i = 1; i < equity_curve.size(); i++)
        r.push_back((equity_curve[i] - equity_curve[i-1]) / equity_curve[i-1]);
    return r;
}

double BacktestResult::daily_returns_mean(const std::vector<double>& d) const {
    if (d.empty()) return 0.0;
    return std::accumulate(d.begin(), d.end(), 0.0) / d.size();
}

double BacktestResult::total_return_pct() const {
    if (equity_curve.empty()) return 0.0;
    return ((equity_curve.back() - initial_cash) / initial_cash) * 100.0;
}

double BacktestResult::annualized_return_pct() const {
    const std::vector<double> d = daily_returns();
    if (d.empty()) return 0.0;
    const double gmean_day_return = geometric_mean_returns(d, true);
    return (std::pow(1.0 + gmean_day_return, 252.0) - 1.0) * 100.0;
}

double BacktestResult::volatility_ann_pct() const {
    const std::vector<double> d = daily_returns();
    if (d.size() < 2) return 0.0;

    const double var = std::pow(sample_stdev(d), 2.0);
    const double gmean = geometric_mean_returns(d, true);
    const double annual_days = 252.0;
    const double term_a = std::pow(var + std::pow(1.0 + gmean, 2.0), annual_days);
    const double term_b = std::pow(1.0 + gmean, 2.0 * annual_days);
    const double vol_ann = std::sqrt(std::max(0.0, term_a - term_b));
    return vol_ann * 100.0;
}

double BacktestResult::sharpe_ratio() const {
    const double vol = volatility_ann_pct();
    if (vol == 0.0) return 0.0;
    return annualized_return_pct() / vol;
}

double BacktestResult::sortino_ratio() const {
    const std::vector<double> d = daily_returns();
    if (d.empty()) return 0.0;

    double downside_ss = 0.0;
    for (double r : d) {
        if (r < 0.0) downside_ss += r * r;
    }
    if (downside_ss == 0.0) return 0.0;
    const double downside_risk_ann =
        std::sqrt(downside_ss / static_cast<double>(d.size())) * std::sqrt(252.0);
    if (downside_risk_ann == 0.0) return 0.0;
    return (annualized_return_pct() / 100.0) / downside_risk_ann;
}

double BacktestResult::max_drawdown_pct() const {
    if (equity_curve.empty()) return 0.0;
    double peak   = std::numeric_limits<double>::lowest();
    double max_dd = 0.0;
    for (size_t i = 0; i < equity_curve.size(); i++) {
        peak = std::max(peak, equity_curve[i]);
        double dd = (equity_curve[i] - peak) / peak;
        max_dd = std::min(max_dd, dd);
    }
    return max_dd * 100.0;
}

double BacktestResult::avg_drawdown_pct() const {
    if (equity_curve.size() < 2) return 0.0;

    std::vector<double> dd(equity_curve.size(), 0.0);
    double peak = equity_curve.front();
    for (size_t i = 0; i < equity_curve.size(); i++) {
        peak = std::max(peak, equity_curve[i]);
        dd[i] = 1.0 - (equity_curve[i] / peak);
    }

    std::vector<size_t> reset_idx;
    reset_idx.reserve(dd.size() + 1);
    for (size_t i = 0; i < dd.size(); i++) {
        if (dd[i] == 0.0) reset_idx.push_back(i);
    }
    if (reset_idx.empty() || reset_idx.back() != dd.size() - 1) {
        reset_idx.push_back(dd.size() - 1);
    }

    double peak_sum = 0.0;
    int periods = 0;
    for (size_t i = 1; i < reset_idx.size(); i++) {
        const size_t prev = reset_idx[i - 1];
        const size_t curr = reset_idx[i];
        if (curr <= prev + 1) continue;
        double segment_peak = 0.0;
        for (size_t j = prev; j <= curr; j++) {
            segment_peak = std::max(segment_peak, dd[j]);
        }
        peak_sum += segment_peak;
        periods++;
    }
    if (periods == 0) return 0.0;
    return -(peak_sum / static_cast<double>(periods)) * 100.0;
}

double BacktestResult::calmar_ratio() const {
    double dd = max_drawdown_pct();
    if (dd == 0.0) return 0.0;
    return annualized_return_pct() / std::abs(dd);
}

double BacktestResult::win_rate_pct() const {
    int sell = std::count_if(trades.begin(), trades.end(),
        [](const Trade& t){ return t.side == Signal::SELL; });
    if (sell == 0) return 0.0;
    int win = std::count_if(trades.begin(), trades.end(),
        [](const Trade& t){ return t.side == Signal::SELL && t.pnl > 0; });
    return static_cast<double>(win) / sell * 100.0;
}

int BacktestResult::total_trades() const {
    return std::count_if(trades.begin(), trades.end(),
        [](const Trade& t){ return t.side == Signal::SELL; });
}

double BacktestResult::best_trade_pct() const {
    if (trades.empty()) return 0.0;
    auto it = std::max_element(trades.begin(), trades.end(),
        [](const Trade& a, const Trade& b){
            if (a.side != Signal::SELL) return true;
            if (b.side != Signal::SELL) return false;
            return a.return_pct < b.return_pct;
        });
    return it->return_pct;
}

double BacktestResult::worst_trade_pct() const {
    if (trades.empty()) return 0.0;
    auto it = std::min_element(trades.begin(), trades.end(),
        [](const Trade& a, const Trade& b){
            if (a.side != Signal::SELL) return false;
            if (b.side != Signal::SELL) return true;
            return a.return_pct < b.return_pct;
        });
    return it->return_pct;
}

double BacktestResult::avg_trade_pct() const {
    int sc = 0;
    double compounded = 1.0;
    for (const Trade& t : trades) {
        if (t.side != Signal::SELL) continue;
        compounded *= (1.0 + t.return_pct / 100.0);
        sc++;
    }
    if (sc == 0 || compounded <= 0.0) return 0.0;
    return (std::pow(compounded, 1.0 / static_cast<double>(sc)) - 1.0) * 100.0;
}

double BacktestResult::buyhold_return_pct() const {
    if (first_close < 1e-9) return 0.0;
    return ((last_close - first_close) / first_close) * 100.0;
}

double BacktestResult::beta() const {
    if (equity_curve.size() < 2 || bench_returns.empty()) return 0.0;
    size_t n = std::min(equity_curve.size() - 1, bench_returns.size());
    if (n < 2) return 0.0;

    std::vector<double> eq_log;
    std::vector<double> mk_log;
    eq_log.reserve(n);
    mk_log.reserve(n);
    for (size_t i = 0; i < n; i++) {
        const double eq_prev = equity_curve[i];
        const double eq_next = equity_curve[i + 1];
        const double mr = bench_returns[i];
        if (eq_prev <= 0.0 || eq_next <= 0.0 || 1.0 + mr <= 0.0) continue;
        eq_log.push_back(std::log(eq_next / eq_prev));
        mk_log.push_back(std::log(1.0 + mr));
    }
    if (eq_log.size() < 2) return 0.0;

    const double me = std::accumulate(eq_log.begin(), eq_log.end(), 0.0) / eq_log.size();
    const double mm = std::accumulate(mk_log.begin(), mk_log.end(), 0.0) / mk_log.size();
    double cov = 0.0;
    double var = 0.0;
    for (size_t i = 0; i < eq_log.size(); i++) {
        cov += (eq_log[i] - me) * (mk_log[i] - mm);
        var += (mk_log[i] - mm) * (mk_log[i] - mm);
    }
    if (var == 0.0) return 0.0;
    return cov / var;
}

double BacktestResult::alpha_pct() const {
    if (benchmark_first_close < 1e-9) return total_return_pct();
    const double benchmark_return =
        ((last_close - benchmark_first_close) / benchmark_first_close) * 100.0;
    return total_return_pct() - beta() * benchmark_return;
}
