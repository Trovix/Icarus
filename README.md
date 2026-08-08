# ICARUS

ICARUS is an autonomous, paper-only convergence trader for equivalent binary
markets on Kalshi and Polymarket. It discovers pairs, watches public order
books, simulates two-leg fills, exits on convergence, and maintains a
recoverable portfolio and P&L ledger in a small terminal dashboard.

It has no authenticated trading integration and cannot place real orders.

## Strategy

For two markets that resolve to the same proposition, ICARUS buys the cheap
outcome on one venue and the complementary outcome on the other. It evaluates
both possible directions at the requested order-book depth and enters only
when the all-in executable edge remains above the configured threshold after
entry fees and slippage buffers.

Open positions are sold into executable bids. Normal convergence exits must
meet the combined-bid target without realizing a net loss after exit costs.
Profit targets, stop losses, maximum holding time, partial fills, and bounded
single-leg exposure are modelled explicitly. If a position reaches resolution,
paper settlement waits for both venues to report the same semantic winner.

See [STRATEGY.md](STRATEGY.md) for the trading model and
[PROJECT_SPEC.md](PROJECT_SPEC.md) for the complete V1 contract.

## Components

- C++17 venue connectors, convergence engine, paper ledger, persistence, and CLI.
- Python market matcher with deterministic blocking and TF-IDF ranking.
- Optional `text-embedding-3-large` candidate scoring and `gpt-5.6-terra`
  structured contract-equivalence judgment.
- Atomic catalog, matcher-output, and portfolio-state replacement.
- Offline C++ and Python test suites.

## Prerequisites

- CMake 3.16 or newer.
- A C++17 compiler.
- libcurl and nlohmann/json discoverable by CMake.
- Python 3.10 or newer.
- An OpenAI API key for autonomous semantic pair acceptance.

With vcpkg, the C++ dependencies can be installed with:

```powershell
vcpkg install curl nlohmann-json
```

## Set up the matcher

Create a virtual environment and install the matcher with its semantic and
numerical dependencies:

```powershell
py -m venv .venv
.\.venv\Scripts\python -m pip install --upgrade pip
.\.venv\Scripts\python -m pip install -e ".[all]"
```

Set the interpreter and API key in the shell that will run ICARUS:

```powershell
$env:ICARUS_PYTHON = (Resolve-Path .\.venv\Scripts\python.exe)
$env:OPENAI_API_KEY = "your-key"
```

The key is read only from the environment. Do not add it to project files.

## Build and test

Configure with the dependency toolchain used on your machine. For vcpkg on
Windows:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON `
  -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
$env:PYTHONPATH = (Resolve-Path matcher)
.\.venv\Scripts\python -m unittest discover -s matcher/tests -v
Remove-Item Env:PYTHONPATH
```

The test suites require neither network access nor paid model calls.

## Run

```powershell
.\build\Release\ICARUS.exe
```

On first run, or when catalogs are stale, the app:

1. downloads public active-market catalogs from both venues;
2. blocks and ranks plausible pairs locally;
3. sends only the bounded candidate set for embeddings and semantic judgment;
4. accepts unambiguous, polarity-aware `openai_judged` pairs; and
5. starts the convergence monitoring loop.

The dashboard shows venue health, accepted pairs, spreads, cash, total equity,
realized and executable unrealized P&L, open lifecycles, residual quantities,
and recent decisions.

Controls:

- `p` pauses or resumes polling.
- `r` runs one cycle immediately.
- `m` refreshes catalogs and reruns pair matching.
- `q` saves state and exits cleanly.

## Configuration

Safe paper defaults are committed in [config/paper.json](config/paper.json).
The most important settings are:

- `entry_spread_threshold`: minimum all-in executable convergence edge.
- `minimum_combined_exit_bid`: raw paired bid target for normal convergence;
  the simulator separately refuses a net-loss convergence exit.
- `trade_quantity`, `profit_target`, `stop_loss`, and `maximum_hold_ms`.
- quote age, cooldown, cash, notional, exposure, position, and orphan limits.
- venue fee, slippage, and simulated-latency assumptions.
- `minimum_match_confidence` and catalog/pair refresh intervals.

Venue fees vary by market and can change. The committed fee rates are zero
because one static formula would be misleading; set realistic fee assumptions
for the markets being simulated before treating reported P&L as meaningful.

Semantic judgment is required by default. Setting
`allow_offline_matching` to `true` explicitly permits lexical-only pairs for
local experiments; it is less safe and is not the autonomous trading default.

## Runtime files

The `data/` directory is generated and ignored by Git. It contains canonical
catalogs, readable market indexes, matcher cache/output, and `paper_state.json`.
Paper state is replaced atomically after portfolio changes and recovered on
restart. Current fee and risk configuration is applied to recovered portfolios
while each already-open trade retains its original exit policy.

To start a deliberately fresh paper portfolio, move `data/paper_state.json`
somewhere safe before launching. Do not delete it while relying on its ledger.

## Safety and limitations

- This is a simulator, not financial advice or a real execution system.
- Pair equivalence is the largest risk; model acceptance is recorded but not
  infallible.
- Public HTTP polling cannot provide exchange-atomic cross-venue snapshots.
- Slippage, latency, fees, and fill priority are estimates.
- Conflicting venue resolution results are never auto-settled; the position is
  retained for investigation.
