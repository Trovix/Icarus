# MVP Specification - ICARUS

## Goal

Build a system that:

- Ingests live prediction market data from two platforms
- Computes cross-platform spreads for equivalent markets
- Detects trade opportunities based on strategy rules
- Simulates execution and tracks positions and PnL

---

## System Responsibilities

The system must:

- Maintain live orderbook views for both venues
- Compute executable prices (bid/ask, not midpoint)
- Continuously compute spread S(t)
- Evaluate entry and exit conditions
- Simulate trades with realistic constraints
- Track all positions and exposures

---

## Definition of an Opportunity

An opportunity must:

- Use executable prices (not midpoints)
- Include fees and buffers
- Use non-stale data
- Have sufficient liquidity on both legs
- Exceed the configured entry threshold

---

## Execution Requirements

Simulation must:

- Use best bid/ask prices
- Apply configurable latency delay
- Support partial fills
- Allow one-leg fills (orphan positions)
- Track unhedged exposure explicitly
- Deduct all/any fees

---

## Position Tracking

The system must:

- Track each leg independently
- Maintain net exposure across venues
- Mark positions using conservative pricing (worst-case exit)
- Update PnL continuously


---

## Constraints

- Binary markets only
- Two venues only (Kalshi and Polymarket)
- Manually approved market pairs
- No real capital deployment
- No automatic semantic matching

---

## Definition of Success (MVP)

The system:

- Runs continuously without crashing
- Produces explainable trade signals
- Avoids false arbitrage signals
- Simulates trades consistently
- Tracks PnL without inconsistencies

---

## Non-Goals

- Perfect execution modelling
- Historical backtesting
- Fully automated trading
- Optimal execution strategies
- High-frequency optimisation

---

## First Deliverable

A running system that:

- Watches a small set of approved pairs
- Displays live bid/ask prices for each venue
- Computes and displays spread S(t)
- Detects entry opportunities
- Simulates trades
- Tracks open positions and PnL in real time