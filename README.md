# Icarus

**Idempotent Cross-Platform Arbitrage Routing and Utility System**

A C++ project to detect and simulate arbitrage opportunities between prediction markets across different platforms.

---

## Overview

Icarus is a system designed to:

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

## Current Status

- Both Kalshi and Polymarket APIs are functional
- Oppurtunity Detection works
- Pair matching suggestions are poor 

Immediate goals:

- Rework pair-finder 
- Implement profit/loss tracker 




