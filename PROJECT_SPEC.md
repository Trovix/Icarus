# ICARUS Paper-Trading V1 Specification

## Objective

ICARUS is an autonomous paper-trading system for cross-venue convergence
trading between equivalent binary markets on Kalshi and Polymarket.

The V1 system discovers equivalent markets, detects temporary price
divergence, simulates two-leg entry and convergence exit, persists its
portfolio, and reports cash, exposure, and profit and loss through a minimal
terminal interface. It never submits real orders.

## Primary Strategy

For an accepted pair with aligned outcomes, ICARUS maintains the executable
YES-price spread:

```text
S(t) = p_K_yes(t) - p_P_yes(t)
```

When Kalshi is expensive, the system buys Polymarket YES and Kalshi NO. When
Polymarket is expensive, it buys Kalshi YES and Polymarket NO. It exits both
legs using executable bids when the spread converges or another exit control
fires.

The configured entry threshold must cover estimated round-trip fees, bid/ask
spread, slippage, and a safety buffer. Holding to resolution is a fallback and
not the normal V1 exit.

## Market Data

The system must:

- Support active binary Kalshi and Polymarket markets.
- Maintain canonical title, description, category, resolution rules, close
  time, outcome labels, venue, and venue market ID.
- Fetch each required order book once per polling cycle.
- Timestamp every snapshot and reject stale or incomplete entry data. Exit
  attempts require only the held-leg bids and must not depend on unrelated
  outcome sides.
- Preserve price and size at every order-book level.
- Treat transport and parsing failures as unavailable data.
- Normalize inverted outcome polarity before calculating spreads.

## Autonomous Pair Discovery

A Python matching worker must:

1. Read canonical venue catalogs.
2. Exclude structurally incompatible markets.
3. Rank candidates with lexical and embedding similarity.
4. Compare the best candidates' complete resolution conditions with a
   structured semantic judge.
5. Determine YES/NO polarity explicitly.
6. Automatically accept only high-confidence, unambiguous matches.
7. Cache embeddings and model decisions.
8. Persist scores, confidence, reason, source hashes, and generation time.

The matcher must have a deterministic offline mode for tests. AI credentials
are read from `OPENAI_API_KEY` and never stored in project files.

## Entry Validation

A convergence entry must satisfy all of the following:

- The pair is active and accepted by the matcher.
- Both books belong to the expected markets and outcomes.
- Both snapshots are within the configured maximum age.
- All-in executable edge at the common paired depth exceeds the entry
  threshold after entry fees and buffers.
- Expected movement toward parity covers estimated exit costs and the safety
  buffer.
- Intended size can be priced from ask depth on both legs.
- Cash and exposure limits permit both legs.
- The same pair/direction is not open or cooling down.

Reference prices may determine direction, but simulated fills and P&L must use
executable depth rather than midpoint prices.

## Exit Management

Every open trade is re-evaluated on each fresh snapshot. Exit conditions are:

- Executable paired bids reach the configured convergence target without
  locking in a net loss after exit fees.
- Executable profit target reached.
- Executable stop loss reached.
- Maximum holding time reached.
- Market closure or defensive data-quality exit.

Closing orders walk bid depth independently. Partial closes and single-leg
closes preserve residual exposure accurately.

## Paper Execution

The simulator must:

- Assign stable IDs to signals, orders, fills, and trades.
- Support buy and sell orders.
- Model each venue leg independently.
- Walk bid/ask depth and support partial fills.
- Apply venue-specific fees and configurable slippage/latency buffers.
- Record requested, filled, and unfilled quantity.
- Represent uneven entry or exit as explicit orphan exposure.
- Never call an authenticated trading endpoint.

## Portfolio, Ledger, and P&L

The system must track:

- Starting and available cash per venue.
- Every simulated order and fill.
- Open and closed convergence trades.
- Positions per venue, market, outcome, and side.
- Hedged and unhedged exposure.
- Entry and exit fees.
- Realized P&L from closes and settlement.
- Conservative executable unrealized P&L.
- Resolution events for positions held past close.

Portfolio state must be written atomically and recoverable after restart.
Repeated processing of a signal, close, or settlement must be idempotent.

## Risk Controls

V1 must support configurable limits for:

- Executable entry-edge and paired-bid exit thresholds.
- Profit target and stop loss.
- Maximum holding period.
- Maximum quote age.
- Maximum quantity per trade.
- Maximum notional per pair, venue, and portfolio.
- Maximum orphan exposure.
- Maximum number of open trades.
- Cooldown between entries on the same pair and direction.

The engine starts in paper mode and cannot be switched to real trading by
configuration.

## Operator Interface

The terminal interface is observational and must show:

- Engine state and venue health.
- Active accepted pairs and current spreads.
- Latest entry/exit signals and simulated fills.
- Cash by venue.
- Open trades, age, entry spread, and current spread.
- Hedged positions and orphan exposure.
- Realized and executable unrealized P&L.

Required controls are pause/resume, one-cycle refresh, catalog rematching, and
graceful quit. Trade approval is not required.

## Persistence and Configuration

- Committed configuration contains safe defaults only.
- Secrets are supplied through environment variables.
- Generated catalogs, model caches, runtime state, and logs are not committed.
- Shared JSON state is replaced atomically.
- Persisted data includes schema versions.

## Required Tests

Automated tests must cover:

- Venue parsing and canonical catalog serialization.
- Candidate blocking, ranking, ambiguity rejection, and polarity.
- Multi-level ask and bid walking.
- Divergence entry and convergence exit decisions.
- Fees, buffers, thresholds, and insufficient liquidity.
- Complete, partial, and orphaned entry/exit fills.
- Profit target, stop loss, and maximum holding exits.
- Cash, positions, realized/unrealized P&L, and settlement.
- Risk-limit and duplicate-signal rejection.
- State save/reload and corrupted-state handling.
- An end-to-end fixture that opens on divergence and closes on convergence.

Tests must not require network access or paid API calls.

## Definition of Complete

Paper-Trading V1 is complete when:

1. A clean checkout can be configured and built using documented commands.
2. All C++ and Python tests pass without network access.
3. The matcher generates accepted, polarity-aware pairs from fixture catalogs.
4. The engine automatically opens a fixture divergence and closes it after
   convergence using executable depth.
5. Restart restores identical cash, positions, fills, trade lifecycle, and P&L.
6. A live read-only smoke run fetches public market data without attempting a
   real order.
7. The CLI explains rejected entries and forced exits.

## Non-Goals

- Real-money or authenticated order submission.
- Automatic capital transfers between venues.
- Multi-outcome, basket, or non-binary trading.
- Predictive directional trading.
- Historical parameter optimization.
- High-frequency or distributed execution.
