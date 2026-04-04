# ICARUS

**Idempotent Cross-Platform Arbitrage Routing and Utility System**

A C++ project to detect and simulate arbitrage opportunities between prediction markets across different platforms.

---

## Overview

ICARUS is a system designed to:

- Ingest live market data from multiple venues (initially Polymarket and Kalshi)
- Use manually approved pairs of semantically equivalent markets
- Detect cross-platform arbitrage opportunities
- Simulate execution (paper trading only for now due to local laws)
- Track positions and PnL

---

## MVP Scope

- 2 venues only: Polymarket and Kalshi 
- Manually approved market pairs only 
- Live data ingestion  
- Arbitrage detection  
- Paper trading / simulated execution  
- Position and PnL tracking  
- Minimal operator interface 

---

## Out of Scope (for now)

- Real-money trading  
- Automatic market matching  
- Multi-outcome or basket arbitrage  
- Backtesting infrastructure  
- Portfolio optimisation  
- Advanced execution strategies  
- Distributed systems or microservices  

---

## Project Structure


```
src/
  core/           # canonical types and utilities
  connectors/     # venue API adapters
  normalisation/  # raw to canonical cleaning
  pairs/          # manually approved mappings
  detector/       # arbitrage logic
  paper_engine/   # simulated execution
  ledger/         # positions and PnL
  app/            # orchestration
  ui/             # minimal interface
```
---


## Current Status

Early Stages

Immediate goals:

- Implement API requests for both platforms   
- Define Strategy Algorithm




