# Strategy - Convergence Trading

## Core Concept

This system doesn't rely on static arbitrage at expiry.

It exploits temporary price divergence between two venues and profits from spread convergence prior to resolution

Define:

S(t) = p_K(t) - p_P(t)

Where:
- p_K(t) = executable price on Kalshi
- p_P(t) = executable price on Polymarket

Profit condition:

S(t_exit) < S(t_entry)

---

## Interpretation

- If S(t) > 0:
  Kalshi is more expensive than Polymarket  
  → Short Kalshi / Long Polymarket

- If S(t) < 0:
  Polymarket is more expensive than Kalshi  
  → Short Polymarket / Long Kalshi

---

## Trade Construction

A trade is a two-leg position across two venues:

- Long the underpriced side
- Short the overpriced side (or synthetically via NO)

Examples:

- Buy YES on Polymarket + Buy NO on Kalshi
- Buy YES on Kalshi + Buy NO on Polymarket

Direction is determined by the sign of S(t).

---

## Entry Condition

Enter when:

|S(t)| > threshold

Threshold must cover:

- Fees (both venues)
- Slippage buffer
- Latency risk

All prices must be executable:
- Use best bid/ask
- Respect orderbook depth

---

## Exit Condition

Exit when ANY of:

- Spread convergence:
  |S(t)| < exit_threshold

- Time stop:
  Trade exceeds maximum duration

- Adverse move:
  Spread widens beyond stop threshold

---

## PnL Definition

PnL is path-dependent:

PnL = S(t_entry) - S(t_exit)

This is independent of market resolution.

---

## Assumptions

- Markets converge over time
- Liquidity is sufficient to enter and exit
- Execution risk is manageable but non-zero

---

## Risks

- Spread widens instead of converging
- One leg fills, the other does not
- Latency causes missed or bad fills
- Markets do not converge before resolution

---

## Non-Goals

- Predicting event outcomes
- Holding to expiry
- Probability modelling of underlying events