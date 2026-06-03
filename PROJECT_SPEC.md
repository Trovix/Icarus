# ICARUS Paper-Trading V1 Specification

## Objective

ICARUS is an autonomous paper-trading system for guaranteed-payout arbitrage
between equivalent binary markets on Kalshi and Polymarket.

The V1 system discovers equivalent markets, validates executable two-leg
opportunities, simulates realistic execution, persists its portfolio, and
reports cash, exposure, and profit and loss through a minimal terminal
interface. It never submits real orders.

## Trading Strategy

For a semantically equivalent pair, ICARUS evaluates both complementary
directions:

- Buy Kalshi YES and Polymarket NO.
- Buy Kalshi NO and Polymarket YES.

An entry is eligible only when the guaranteed binary payout exceeds the full
executable cost:

```text
net_edge = 1.00 - leg_1_cost - leg_2_cost - fees - safety_buffer
```

`net_edge` must exceed the configured minimum. Prices must be obtained by
walking available ask depth for the intended quantity; midpoint prices are
never executable prices.

Positions are normally held until resolution. V1 may unwind an orphaned leg
according to its risk policy, but convergence trading is not part of V1.

## Market Data

The system must:

- Support active binary Kalshi and Polymarket markets.
- Maintain canonical market metadata including title, description, category,
  resolution rules, close time, outcome labels, venue, and venue market ID.
- Fetch each required order book once per polling cycle.
- Timestamp every snapshot and reject stale or incomplete data.
- Preserve price and size at every order-book level.
- Treat transport and parsing failures as unavailable data, never as an empty
  but tradable book.

## Autonomous Pair Discovery

A Python matching worker must:

1. Read canonical venue catalogs.
2. Exclude structurally incompatible markets.
3. Rank candidates with lexical and embedding similarity.
4. Compare the best candidates' full resolution conditions with a structured
   semantic judge.
5. Determine YES/NO polarity explicitly.
6. Automatically accept only high-confidence, unambiguous matches.
7. Cache embeddings and model decisions.
8. Persist match score, confidence, reason, source hashes, and generation time.

The matcher must have a deterministic offline mode for development and tests.
When AI matching is enabled, credentials are read from `OPENAI_API_KEY` and are
never stored in project files.

## Signal Validation

An opportunity must satisfy all of the following:

- The pair is active and accepted by the matcher.
- Both books belong to the expected markets and outcomes.
- Both snapshots are within the configured maximum age.
- The intended size can be priced from real order-book depth.
- Net edge remains above the configured threshold after fees and buffer.
- Cash and risk limits permit both legs.
- The same signal is not already open or inside its cooldown period.

## Paper Execution

The simulator must:

- Assign stable IDs to signals, orders, fills, and trades.
- Model each venue leg independently.
- Walk book depth and support partial fills.
- Apply venue-specific fees and a configurable safety buffer.
- Record requested, filled, and unfilled quantity.
- Represent a one-leg or uneven fill as explicit orphan exposure.
- Apply the configured orphan policy without inventing liquidity.
- Never call an authenticated trading endpoint.

## Portfolio, Ledger, and P&L

The system must track:

- Starting and available cash per venue.
- Every simulated order and fill.
- Positions per venue, market, and outcome.
- Hedged and unhedged exposure.
- Fees paid.
- Realized and unrealized P&L.
- Guaranteed payout and locked-in P&L for hedged positions.
- Resolution and settlement events.

Portfolio state must be persisted atomically and recoverable after a restart.
Repeated processing of the same signal or settlement must be idempotent.

## Risk Controls

V1 must support configurable limits for:

- Minimum net edge.
- Maximum quote age.
- Maximum quantity per trade.
- Maximum notional per pair.
- Maximum notional per venue.
- Maximum total exposure.
- Maximum number of open trades.
- Cooldown between entries on the same pair and direction.

The engine starts in paper mode and cannot be switched to real trading by
configuration.

## Operator Interface

The terminal interface is observational and must show:

- Engine state and venue health.
- Active accepted pairs.
- Latest signals and simulated fills.
- Cash by venue.
- Open positions and orphan exposure.
- Realized, unrealized, and locked-in P&L.

The only required operator controls are pause/resume, one-cycle refresh, and
graceful quit. Trade approval is not required.

## Persistence and Configuration

- Committed configuration contains safe defaults only.
- Secrets are supplied through environment variables.
- Generated catalogs, model caches, runtime state, and logs are not committed.
- Writes that replace shared JSON state use a temporary file and atomic rename.
- Schema versions are included in persisted data.

## Required Tests

Automated tests must cover:

- Venue parsing and canonical catalog serialization.
- Candidate blocking, ranking, ambiguity rejection, and polarity.
- Top-of-book and multi-level book walking.
- Fees, buffers, thresholds, and insufficient liquidity.
- Complete, partial, and orphaned two-leg fills.
- Cash, positions, P&L, settlement, and idempotency.
- Risk-limit and duplicate-signal rejection.
- State save/reload and corrupted-state handling.
- An end-to-end fixture that discovers a pair and completes a paper trade.

Tests must not require network access or paid API calls.

## Definition of Complete

Paper-Trading V1 is complete when:

1. A clean checkout can be configured and built using documented commands.
2. All C++ and Python tests pass without network access.
3. The matcher can generate accepted pairs from fixture catalogs.
4. The engine can automatically detect and simulate a fixture opportunity.
5. Restarting restores identical cash, positions, fills, and P&L.
6. A live read-only smoke run can fetch supported public market data without
   attempting a real order.
7. The CLI explains why no trade occurs when data or risk checks fail.

## Non-Goals

- Real-money or authenticated order submission.
- Automatic capital transfers between venues.
- Multi-outcome, basket, or non-binary arbitrage.
- Predictive directional trading.
- Convergence trading.
- Historical strategy optimization.
- High-frequency or distributed execution.

