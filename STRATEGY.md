# Strategy - Guaranteed-Payout Cross-Venue Arbitrage

ICARUS Paper-Trading V1 buys complementary outcomes on semantically equivalent
binary markets when their all-in executable cost is less than the guaranteed
resolution payout.

## Trade Construction

For every accepted Kalshi/Polymarket pair, evaluate both directions:

- Buy Kalshi YES and Polymarket NO.
- Buy Kalshi NO and Polymarket YES.

If the markets resolve under genuinely equivalent rules, exactly one of the two
legs pays `1.00` per complete pair.

## Entry Condition

For an intended quantity, walk the available ask depth on both venues and
calculate:

```text
net_edge = 1.00 - leg_1_cost - leg_2_cost - fees - safety_buffer
```

Enter only when:

- `net_edge` exceeds the configured minimum;
- both books are complete and fresh;
- sufficient liquidity and paper cash are available;
- portfolio risk limits permit the trade; and
- the pair and direction are not already open or cooling down.

Midpoint prices and displayed prices without size are not executable prices.

## Execution and Exit

Each venue leg is simulated independently. Unequal or partial fills create
explicit orphan exposure and invoke the configured risk policy.

Hedged positions are normally held through resolution and settled against the
guaranteed payout. V1 does not assume that spread convergence will provide an
earlier exit.

## P&L

For a fully hedged quantity before settlement:

```text
locked_in_pnl = guaranteed_payout - entry_cost - fees
```

Unhedged positions are marked conservatively using executable exit prices.
Realized P&L changes only through simulated closing fills or settlement.

## Primary Risks

- Incorrect semantic pairing or different settlement rules.
- Stale or incomplete order books.
- Insufficient depth at the displayed price.
- One venue leg filling without the other.
- Incorrect fee or resolution modelling.

These risks must remain visible in the pair audit trail, paper ledger, and CLI.

