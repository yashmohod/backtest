#include "../include/backtest.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>


int tests_run = 0;
int tests_passed = 0;

void check(bool condition, const std::string& test_name) {
    tests_run++;
    if (condition) {
        tests_passed++;
        std::cout << "  PASS  " << test_name << "\n";
    } else {
        std::cout << "  FAIL  " << test_name << "\n";
    }
}

bool approx(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

template <typename ExceptionType, typename Func>
bool throws(Func f) {
    try { f(); return false; }
    catch (const ExceptionType&) { return true; }
    catch (...) { return false; }
}

class RSI_Crossover: public Strategy{
  
  private:
    const double upper_bound_ = 70;
    const double lower_bound_ = 30;
    int period_ = 14;
    mutable double average_gain_ = 0;
    mutable double average_loss_ = 0;
    mutable double prev_rsi_ = 0;
    mutable bool has_prev_rsi_ = false;
    mutable bool seeded_ = false;

  public:
    RSI_Crossover()=default;
    RSI_Crossover(int period){
      if(period < 1)
        throw std::invalid_argument("RSI period can't be less than 1");
      period_ = period;
    
    }

    size_t warmup_bars() const override { return static_cast<size_t>(period_); }

    Signal next(const Context& ctx)const override{
      if (ctx.current_index == 0) return Signal::HOLD;

      if (!seeded_) {
        if (ctx.current_index < static_cast<size_t>(period_)) return Signal::HOLD;

        double gain_sum = 0.0;
        double loss_sum = 0.0;
        for (int i = 0; i < period_; i++) {
          const double diff = ctx.data[i + 1].close - ctx.data[i].close;
          if (diff > 0.0) gain_sum += diff;
          else loss_sum += -diff;
        }
        average_gain_ = gain_sum / period_;
        average_loss_ = loss_sum / period_;
        seeded_ = true;
      } else {
        const double diff = ctx.close() - ctx.close_ago(1);
        const double gain = diff > 0.0 ? diff : 0.0;
        const double loss = diff < 0.0 ? -diff : 0.0;
        average_gain_ = ((average_gain_ * (period_ - 1)) + gain) / period_;
        average_loss_ = ((average_loss_ * (period_ - 1)) + loss) / period_;
      }

      const double rsi = (average_loss_ == 0.0) ? 100.0
                       : 100.0 - (100.0 / (1.0 + (average_gain_ / average_loss_)));

      Signal signal = Signal::HOLD;
      if (has_prev_rsi_) {
        //if (prev_rsi_ < upper_bound_ && rsi >= upper_bound_) signal = Signal::SELL;
        //else if (prev_rsi_ < lower_bound_ && rsi >= lower_bound_) signal = Signal::BUY;
        if(crossover(prev_rsi_,rsi,lower_bound_)) signal = Signal::BUY;
        else if(crossover(prev_rsi_,rsi,upper_bound_)) signal = Signal::SELL;
      }
      prev_rsi_ = rsi;
      has_prev_rsi_ = true;
      return signal;
    }
};

class SMA_Crossover: public Strategy{

  private:
    int fast_period_ = 20;
    int slow_period_ = 50;
    mutable double fast_moving_average_ = 0;
    mutable double slow_moving_average_ = 0;
    mutable double prev_fast_ = 0;
    mutable double prev_slow_ = 0;

  public:
    SMA_Crossover()=default;
    SMA_Crossover(int f_period, int s_period){
      if(f_period < 1 || s_period < 1)
        throw std::invalid_argument("SMA period can't be less than 1");
      
      if(s_period < f_period )
        throw std::invalid_argument("Fast moving average can't be larger than slow moving average!");

      fast_period_ = f_period;
      slow_period_ = s_period;
    
    }
size_t warmup_bars() const override {
    return slow_period_ > 0 ? static_cast<size_t>(slow_period_ - 1) : 0;
}

Signal next(const Context& ctx) const override {
    if(ctx.current_index < (size_t)slow_period_)
        return Signal::HOLD;

    // compute SMAs directly every bar — no rolling update, no drift
    double fast_sum = 0;
    double slow_sum = 0;
    for(int i = 0; i < fast_period_; i++)
        fast_sum += ctx.close_ago(i);
    for(int i = 0; i < slow_period_; i++)
        slow_sum += ctx.close_ago(i);

    double fast_sma = fast_sum / fast_period_;
    double slow_sma = slow_sum / slow_period_;

    // need previous bar's SMAs for crossover detection
    double prev_fast_sum = 0;
    double prev_slow_sum = 0;
    for(int i = 1; i <= fast_period_; i++)
        prev_fast_sum += ctx.close_ago(i);
    for(int i = 1; i <= slow_period_; i++)
        prev_slow_sum += ctx.close_ago(i);

    double prev_fast_sma = prev_fast_sum / fast_period_;
    double prev_slow_sma = prev_slow_sum / slow_period_;

    if(crossover(prev_slow_sma, prev_fast_sma, slow_sma, fast_sma))
        return Signal::SELL;
    if(crossover(prev_fast_sma, prev_slow_sma, fast_sma, slow_sma))
        return Signal::BUY;

    return Signal::HOLD;
}};


std::unordered_map<std::string,double> load_expected(const std::string& path){
  std::string line; 
  std::ifstream file(path);
  std::unordered_map<std::string,double> expected;
  std::getline(file, line); // skip header
  while(std::getline(file,line)){
    std::istringstream ss(line);
    std::string key, value;
    std::getline(ss,key,',');
    std::getline(ss,value);
    if (value.empty()) continue;
    expected[key] = std::stod(value);
  }
  file.close();
  return expected;
}


void AAPL_tests(){
  
  // Expected values from backtest.py 
  std::unordered_map<std::string,double> sma_expected = load_expected("./test/test_data/AAPL_sma_expected.csv");
  std::unordered_map<std::string,double> rsi_expected = load_expected("./test/test_data/AAPL_rsi_expected.csv");

  BacktestConfig config;
  config.initial_cash = 1000.0;
  config.commission   = 0.0;

  PriceData aapl = load_csv("./test/test_data/AAPL.csv");

  // SMA setup
  SMA_Crossover sma_strategy(20, 50);
  BacktestResult aapl_sma = run_backtest(aapl, sma_strategy, config);

  // RSI setup
  RSI_Crossover rsi_strategy(14);
  BacktestResult aapl_rsi = run_backtest(aapl, rsi_strategy, config);

  // AAPL SMA tests
  check(approx(aapl_sma.total_return_pct(),      sma_expected["return_pct"]),            "AAPL SMA total return");
  check(approx(aapl_sma.annualized_return_pct(), sma_expected["annualized_return_pct"]), "AAPL SMA annualized return");
  check(approx(aapl_sma.sharpe_ratio(),          sma_expected["sharpe_ratio"]),           "AAPL SMA sharpe");
  check(approx(aapl_sma.sortino_ratio(),         sma_expected["sortino_ratio"]),          "AAPL SMA sortino");
  check(approx(aapl_sma.calmar_ratio(),          sma_expected["calmar_ratio"]),           "AAPL SMA calmar");
  check(approx(aapl_sma.max_drawdown_pct(),      sma_expected["max_drawdown_pct"]),       "AAPL SMA max drawdown");
  check(approx(aapl_sma.avg_drawdown_pct(),      sma_expected["avg_drawdown_pct"]),       "AAPL SMA avg drawdown");
  check(approx(aapl_sma.volatility_ann_pct(),    sma_expected["volatility_ann_pct"]),     "AAPL SMA volatility");
  check(approx(aapl_sma.win_rate_pct(),          sma_expected["win_rate_pct"]),           "AAPL SMA win rate");
  check(approx(aapl_sma.total_trades(),          sma_expected["total_trades"]),           "AAPL SMA total trades");
  check(approx(aapl_sma.best_trade_pct(),        sma_expected["best_trade_pct"]),         "AAPL SMA best trade");
  check(approx(aapl_sma.worst_trade_pct(),       sma_expected["worst_trade_pct"]),        "AAPL SMA worst trade");
  check(approx(aapl_sma.avg_trade_pct(),         sma_expected["avg_trade_pct"]),          "AAPL SMA avg trade");
  // Backtesting.py buy-and-hold uses internal warmup-aware equity semantics.
  // We intentionally do not parity-test this metric in this engine.
  check(approx(aapl_sma.alpha_pct(),             sma_expected["alpha_pct"]),              "AAPL SMA alpha");
  check(approx(aapl_sma.beta(),                  sma_expected["beta"]),                   "AAPL SMA beta");
  
  // AAPL RSI tests
  check(approx(aapl_rsi.total_return_pct(),      rsi_expected["return_pct"]),            "AAPL RSI total return");
  check(approx(aapl_rsi.annualized_return_pct(), rsi_expected["annualized_return_pct"]), "AAPL RSI annualized return");
  check(approx(aapl_rsi.sharpe_ratio(),          rsi_expected["sharpe_ratio"]),           "AAPL RSI sharpe");
  check(approx(aapl_rsi.sortino_ratio(),         rsi_expected["sortino_ratio"]),          "AAPL RSI sortino");
  check(approx(aapl_rsi.calmar_ratio(),          rsi_expected["calmar_ratio"]),           "AAPL RSI calmar");
  check(approx(aapl_rsi.max_drawdown_pct(),      rsi_expected["max_drawdown_pct"]),       "AAPL RSI max drawdown");
  check(approx(aapl_rsi.avg_drawdown_pct(),      rsi_expected["avg_drawdown_pct"]),       "AAPL RSI avg drawdown");
  check(approx(aapl_rsi.volatility_ann_pct(),    rsi_expected["volatility_ann_pct"]),     "AAPL RSI volatility");
  check(approx(aapl_rsi.win_rate_pct(),          rsi_expected["win_rate_pct"]),           "AAPL RSI win rate");
  check(approx(aapl_rsi.total_trades(),          rsi_expected["total_trades"]),           "AAPL RSI total trades");
  check(approx(aapl_rsi.best_trade_pct(),        rsi_expected["best_trade_pct"]),         "AAPL RSI best trade");
  check(approx(aapl_rsi.worst_trade_pct(),       rsi_expected["worst_trade_pct"]),        "AAPL RSI worst trade");
  check(approx(aapl_rsi.avg_trade_pct(),         rsi_expected["avg_trade_pct"]),          "AAPL RSI avg trade");
  // buy-and-hold parity intentionally skipped (see note above).
  check(approx(aapl_rsi.alpha_pct(),             rsi_expected["alpha_pct"]),              "AAPL RSI alpha");
  check(approx(aapl_rsi.beta(),                  rsi_expected["beta"]),                   "AAPL RSI beta");

}

void GOOG_tests(){
  
  std::unordered_map<std::string,double> sma_expected = load_expected("./test/test_data/GOOG_sma_expected.csv");
  std::unordered_map<std::string,double> rsi_expected = load_expected("./test/test_data/GOOG_rsi_expected.csv");

  BacktestConfig config;
  config.initial_cash = 1000.0;
  config.commission   = 0.0;

  PriceData goog = load_csv("./test/test_data/GOOG.csv");

  SMA_Crossover sma_strategy(20, 50);
  BacktestResult goog_sma = run_backtest(goog, sma_strategy, config);

  RSI_Crossover rsi_strategy(14);
  BacktestResult goog_rsi = run_backtest(goog, rsi_strategy, config);

  // GOOG SMA tests
  check(approx(goog_sma.total_return_pct(),      sma_expected["return_pct"]),            "GOOG SMA total return");
  check(approx(goog_sma.annualized_return_pct(), sma_expected["annualized_return_pct"]), "GOOG SMA annualized return");
  check(approx(goog_sma.sharpe_ratio(),          sma_expected["sharpe_ratio"]),           "GOOG SMA sharpe");
  check(approx(goog_sma.sortino_ratio(),         sma_expected["sortino_ratio"]),          "GOOG SMA sortino");
  check(approx(goog_sma.calmar_ratio(),          sma_expected["calmar_ratio"]),           "GOOG SMA calmar");
  check(approx(goog_sma.max_drawdown_pct(),      sma_expected["max_drawdown_pct"]),       "GOOG SMA max drawdown");
  check(approx(goog_sma.avg_drawdown_pct(),      sma_expected["avg_drawdown_pct"]),       "GOOG SMA avg drawdown");
  check(approx(goog_sma.volatility_ann_pct(),    sma_expected["volatility_ann_pct"]),     "GOOG SMA volatility");
  check(approx(goog_sma.win_rate_pct(),          sma_expected["win_rate_pct"]),           "GOOG SMA win rate");
  check(approx(goog_sma.total_trades(),          sma_expected["total_trades"]),           "GOOG SMA total trades");
  check(approx(goog_sma.best_trade_pct(),        sma_expected["best_trade_pct"]),         "GOOG SMA best trade");
  check(approx(goog_sma.worst_trade_pct(),       sma_expected["worst_trade_pct"]),        "GOOG SMA worst trade");
  check(approx(goog_sma.avg_trade_pct(),         sma_expected["avg_trade_pct"]),          "GOOG SMA avg trade");
  // buy-and-hold parity intentionally skipped (see AAPL note).
  check(approx(goog_sma.alpha_pct(),             sma_expected["alpha_pct"]),              "GOOG SMA alpha");
  check(approx(goog_sma.beta(),                  sma_expected["beta"]),                   "GOOG SMA beta");

  // GOOG RSI tests
  check(approx(goog_rsi.total_return_pct(),      rsi_expected["return_pct"]),            "GOOG RSI total return");
  check(approx(goog_rsi.annualized_return_pct(), rsi_expected["annualized_return_pct"]), "GOOG RSI annualized return");
  check(approx(goog_rsi.sharpe_ratio(),          rsi_expected["sharpe_ratio"]),           "GOOG RSI sharpe");
  check(approx(goog_rsi.sortino_ratio(),         rsi_expected["sortino_ratio"]),          "GOOG RSI sortino");
  check(approx(goog_rsi.calmar_ratio(),          rsi_expected["calmar_ratio"]),           "GOOG RSI calmar");
  check(approx(goog_rsi.max_drawdown_pct(),      rsi_expected["max_drawdown_pct"]),       "GOOG RSI max drawdown");
  check(approx(goog_rsi.avg_drawdown_pct(),      rsi_expected["avg_drawdown_pct"]),       "GOOG RSI avg drawdown");
  check(approx(goog_rsi.volatility_ann_pct(),    rsi_expected["volatility_ann_pct"]),     "GOOG RSI volatility");
  check(approx(goog_rsi.win_rate_pct(),          rsi_expected["win_rate_pct"]),           "GOOG RSI win rate");
  check(approx(goog_rsi.total_trades(),          rsi_expected["total_trades"]),           "GOOG RSI total trades");
  check(approx(goog_rsi.best_trade_pct(),        rsi_expected["best_trade_pct"]),         "GOOG RSI best trade");
  check(approx(goog_rsi.worst_trade_pct(),       rsi_expected["worst_trade_pct"]),        "GOOG RSI worst trade");
  check(approx(goog_rsi.avg_trade_pct(),         rsi_expected["avg_trade_pct"]),          "GOOG RSI avg trade");
  // buy-and-hold parity intentionally skipped (see AAPL note).
  check(approx(goog_rsi.alpha_pct(),             rsi_expected["alpha_pct"]),              "GOOG RSI alpha");
  check(approx(goog_rsi.beta(),                  rsi_expected["beta"]),                   "GOOG RSI beta");
}

int main() {
    // your tests here
    AAPL_tests();
    GOOG_tests();

    std::cout << tests_passed << "/" << tests_run << " tests passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}




















