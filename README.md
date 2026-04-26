# C++ Backtesting Analytics Engine

Minimal C++ backtesting engine that runs user-defined strategies over OHLCV bars
and reports portfolio/trade analytics aligned as closely as possible with
`backtesting.py`.

## Current Scope

- Next-bar execution model: strategy signal on bar `N`, order filled at bar `N+1` open.
- Long-only, all-in sizing: `shares = floor(cash / entry_price)`.
- Forced close on final bar open if a position remains open.
- Metrics and trade logs for both strategy return and market benchmark.

## Data Format

CSV columns are parsed in this order:

`Date,Close,High,Low,Open,Volume`

## Build And Run

Build tests:

`g++ -std=c++17 -O2 test/backtest_tests.cpp -o backtester_tests`

Run tests:

`./backtester_tests`

Build sample executable:

`g++ -std=c++17 -O2 main.cpp -o backtest_demo`

Run sample:

`./backtest_demo`

## Methodology Notes

- Annualized return is compounded from total return across trading days.
- Volatility, Sharpe, and Sortino use `backtesting.py`-style annualized formulas.
- Average trade return uses geometric mean of closed-trade returns.
- Average drawdown is computed from drawdown-period peaks (not per-bar averaging).

## Known Non-Parity Item

- `Buy & Hold Return [%]` is intentionally not parity-tested, because
  `backtesting.py` computes this with internal warmup-aware equity semantics that
  differ from this engine's direct close-to-close baseline.
