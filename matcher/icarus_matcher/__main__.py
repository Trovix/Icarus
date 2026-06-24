from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Sequence

from .cache import MatcherCache
from .io import atomic_write_json, load_catalog, result_payload
from .openai_backend import OpenAIBackend
from .pipeline import Matcher, MatcherConfig


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m icarus_matcher",
        description="Pair equivalent Kalshi and Polymarket binary markets.",
    )
    parser.add_argument("--kalshi-catalog", required=True, type=Path)
    parser.add_argument("--polymarket-catalog", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("data/market_pairs.json"))
    parser.add_argument("--cache", type=Path, default=Path("data/matcher_cache.sqlite3"))
    parser.add_argument("--offline", action="store_true", help="Never make OpenAI API calls")
    parser.add_argument("--candidate-limit", type=int, default=5)
    parser.add_argument("--candidate-threshold", type=float, default=0.20)
    parser.add_argument("--accept-threshold", type=float, default=0.72)
    parser.add_argument("--ambiguity-margin", type=float, default=0.08)
    parser.add_argument("--date-tolerance-days", type=int, default=14)
    parser.add_argument("--embedding-model", default="text-embedding-3-large")
    parser.add_argument("--judge-model", default="gpt-5.6-terra")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        config = MatcherConfig(
            candidate_limit=args.candidate_limit,
            candidate_threshold=args.candidate_threshold,
            accept_threshold=args.accept_threshold,
            ambiguity_margin=args.ambiguity_margin,
            date_tolerance_days=args.date_tolerance_days,
        )
        kalshi = load_catalog(args.kalshi_catalog, "kalshi")
        polymarket = load_catalog(args.polymarket_catalog, "polymarket")

        with MatcherCache(args.cache) as cache:
            backend = None
            if not args.offline and os.environ.get("OPENAI_API_KEY"):
                backend = OpenAIBackend(
                    cache,
                    embedding_model=args.embedding_model,
                    judge_model=args.judge_model,
                )
            matcher = Matcher(config=config, openai_backend=backend)
            result = matcher.match(kalshi, polymarket)
        atomic_write_json(args.output, result_payload(result))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f"error: {error}\n")

    summary = {
        "mode": result.mode,
        "pairs": len(result.pairs),
        "candidates": result.candidate_count,
        "output": str(args.output),
    }
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

