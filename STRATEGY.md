# Strategy - Cross-Venue Convergence Trading

ICARUS Paper-Trading V1 exploits temporary price divergence between
semantically equivalent binary markets on Kalshi and Polymarket. It enters a
market-neutral two-leg position and exits when the executable spread converges.

Holding to resolution is a safety fallback, not the primary strategy.

## Spread

For aligned YES outcomes, define:

```text
S(t) = p_K_yes(t) - p_P_yes(t)
```

The reference prices used to decide direction are derived from current bid/ask
quotes. Actual entries and exits are always evaluated using executable order
book depth, fees, and buffers.

## Trade Construction

If `S(t) > 0`, Kalshi YES is more expensive:

- Buy Polymarket YES.
- Buy Kalshi NO as the synthetic short leg.

If `S(t) < 0`, Polymarket YES is more expensive:

- Buy Kalshi YES.
- Buy Polymarket NO as the synthetic short leg.

The semantic matcher records outcome polarity. Prices are transformed before
spread calculation when the two venues phrase opposite propositions.

## Entry

The engine prices both complementary directions at the common executable
quantity. Define the all-in entry edge as:

```text
E_entry = 1 - (Kalshi leg debit + Polymarket leg debit) / paired quantity
```

The debits include configured entry fees and buffers. Enter only when:

- `E_entry` exceeds the configured entry threshold;
- the expected convergence move exceeds estimated round-trip fees, spread,
  slippage, and safety buffer;
- both books are fresh and have sufficient executable depth;
- risk limits permit both legs; and
- the pair and direction are not already open or cooling down.

The simulator walks asks to open both legs. Unequal fills create explicit
orphan exposure and invoke the configured orphan policy.

## Exit

The normal exit occurs when any configured condition is met:

- executable paired bids reach the convergence target and the close is not a
  net loss after exit fees;
- executable mark-to-market profit reaches its target;
- loss reaches the stop-loss limit;
- the maximum holding period expires; or
- market closure or data quality requires a defensive exit.

Both legs are closed using executable bids and available depth. A partially
closed pair remains open with its residual hedged or orphan exposure recorded.

## P&L

For a closed paired quantity:

```text
realized_pnl = closing_proceeds - opening_cost - entry_fees - exit_fees
```

Open positions are marked conservatively using current executable bid depth.
The CLI reports realized P&L, executable unrealized P&L, fees, and orphan
exposure separately.

## Resolution

If a position cannot be closed before resolution, settlement pays the winning
outcome according to venue rules. Semantic mismatches are therefore the most
important risk and must remain visible in the pair audit trail.

## Primary Risks

- The spread widens instead of converging.
- Pair settlement conditions are not truly equivalent.
- Displayed prices lack executable depth.
- One venue leg fills or closes without the other.
- Quotes become stale during entry or exit.
- Fees or resolution behavior are modelled incorrectly.
