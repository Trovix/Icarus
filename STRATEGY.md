# Strategy - Convergence Trading


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
- Short the overpriced side (synthetically via NO trade)

Either:
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
- Latency risk (we dont want to buy into markets resolving in large t)


---

## Exit Condition

Exit when:

- Spread convergence:
  |S(t)| < exit_threshold


---

## PnL Definition

PnL is path-dependent:

PnL = S(t_entry) - S(t_exit)

This is independent of market resolution. But S(t_exit) is guarenteed to be 0 if t_exit = t_resolution.

---

## Risks

- Spread widens instead of converging (only an issue for small t)
- Markets do not converge at resolution (extremely unlikely unless pairs were chosen incorrectly)

---

## Non-Goals

- Predicting event outcomes (for now) in future consider weighting based on market cap- indicator of correctness??
- Holding to expiry as a strategy (we want to exit earlier to increase profit/time)
