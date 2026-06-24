from __future__ import annotations

import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .models import Market, parse_catalog
from .pipeline import MatchResult


SCHEMA_VERSION = 1


def load_catalog(path: str | Path, venue: str) -> list[Market]:
    source = Path(path)
    try:
        with source.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON in {source}: {error}") from error
    if venue not in {"kalshi", "polymarket"}:
        raise ValueError(f"unsupported venue: {venue}")
    return parse_catalog(payload, venue)  # type: ignore[arg-type]


def result_payload(result: MatchResult) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "matcher": {
            "mode": result.mode,
            "candidate_count": result.candidate_count,
            "active_market_counts": {
                "kalshi": result.active_kalshi_count,
                "polymarket": result.active_polymarket_count,
            },
        },
        "pairs": [pair.to_dict() for pair in result.pairs],
    }


def atomic_write_json(path: str | Path, payload: dict[str, Any]) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary_path = Path(handle.name)
            json.dump(payload, handle, indent=2, sort_keys=True, ensure_ascii=False)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, destination)
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()

