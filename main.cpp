#include "includes/backtester.hpp"
#include <iostream>

int main() {
    PriceData aapl = load_csv("data/AAPL.csv");
    PriceData goog = load_csv("data/GOOG.csv");

    BacktestConfig config;
    config.initial_cash = 10000.0;
    config.commission   = 0.0;

    // --- AAPL: SMA Crossover (fast=20, slow=50) ---
    SMA_Crossover sma_strategy(20, 50);
    BacktestResult aapl_sma = run_backtest(aapl, sma_strategy, config);

    std::cout << "=== AAPL SMA Crossover ===\n";
    std::cout << "Total Return:       " << aapl_sma.total_return_pct()      << "%\n";
    std::cout << "Annualized Return:  " << aapl_sma.annualized_return_pct() << "%\n";
    std::cout << "Sharpe Ratio:       " << aapl_sma.sharpe_ratio()          << "\n";
    std::cout << "Sortino Ratio:      " << aapl_sma.sortino_ratio()         << "\n";
    std::cout << "Calmar Ratio:       " << aapl_sma.calmar_ratio()          << "\n";
    std::cout << "Max Drawdown:       " << aapl_sma.max_drawdown_pct()      << "%\n";
    std::cout << "Volatility (Ann.):  " << aapl_sma.volatility_ann_pct()    << "%\n";
    std::cout << "Win Rate:           " << aapl_sma.win_rate_pct()          << "%\n";
    std::cout << "Total Trades:       " << aapl_sma.total_trades()          << "\n";
    std::cout << "Best Trade:         " << aapl_sma.best_trade_pct()        << "%\n";
    std::cout << "Worst Trade:        " << aapl_sma.worst_trade_pct()       << "%\n";
    std::cout << "Avg Trade:          " << aapl_sma.avg_trade_pct()         << "%\n";
    std::cout << "Buy & Hold Return:  " << aapl_sma.buyhold_return_pct()    << "%\n";
    std::cout << "Alpha:              " << aapl_sma.alpha_pct()             << "%\n";
    std::cout << "Beta:               " << aapl_sma.beta()                  << "\n";

    // --- GOOG: RSI Crossover (period=14) ---
    RSI_Crossover rsi_strategy(14);
    BacktestResult goog_rsi = run_backtest(goog, rsi_strategy, config);

    std::cout << "\n=== GOOG RSI Crossover ===\n";
    std::cout << "Total Return:       " << goog_rsi.total_return_pct()      << "%\n";
    std::cout << "Annualized Return:  " << goog_rsi.annualized_return_pct() << "%\n";
    std::cout << "Sharpe Ratio:       " << goog_rsi.sharpe_ratio()          << "\n";
    std::cout << "Sortino Ratio:      " << goog_rsi.sortino_ratio()         << "\n";
    std::cout << "Calmar Ratio:       " << goog_rsi.calmar_ratio()          << "\n";
    std::cout << "Max Drawdown:       " << goog_rsi.max_drawdown_pct()      << "%\n";
    std::cout << "Volatility (Ann.):  " << goog_rsi.volatility_ann_pct()    << "%\n";
    std::cout << "Win Rate:           " << goog_rsi.win_rate_pct()          << "%\n";
    std::cout << "Total Trades:       " << goog_rsi.total_trades()          << "\n";
    std::cout << "Best Trade:         " << goog_rsi.best_trade_pct()        << "%\n";
    std::cout << "Worst Trade:        " << goog_rsi.worst_trade_pct()       << "%\n";
    std::cout << "Avg Trade:          " << goog_rsi.avg_trade_pct()         << "%\n";
    std::cout << "Buy & Hold Return:  " << goog_rsi.buyhold_return_pct()    << "%\n";
    std::cout << "Alpha:              " << goog_rsi.alpha_pct()             << "%\n";
    std::cout << "Beta:               " << goog_rsi.beta()                  << "\n";

    // --- TRADE LOGS ---
    std::cout << "\n=== AAPL SMA Trade Log ===\n";
    for (const Trade& t : aapl_sma.trades) {
        std::cout << t.date  << "  "
                  << (t.side == Signal::BUY ? "BUY " : "SELL")
                  << "  price: $" << t.price
                  << "  shares: " << t.shares
                  << "  pnl: $"   << t.pnl
                  << "\n";
    }

    std::cout << "\n=== GOOG RSI Trade Log ===\n";
    for (const Trade& t : goog_rsi.trades) {
        std::cout << t.date  << "  "
                  << (t.side == Signal::BUY ? "BUY " : "SELL")
                  << "  price: $" << t.price
                  << "  shares: " << t.shares
                  << "  pnl: $"   << t.pnl
                  << "\n";
    }

    return 0;
}
