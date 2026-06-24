from __future__ import annotations

import json
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from icarus_matcher.__main__ import main
from icarus_matcher.cache import MatcherCache
from icarus_matcher.models import Market, parse_catalog
from icarus_matcher.openai_backend import SemanticJudgment
from icarus_matcher.pipeline import Matcher, MatcherConfig, block_reason
from icarus_matcher.similarity import tfidf_cosine_matrix


def market(venue: str, market_id: str, title: str, **changes: object) -> Market:
    values = {
        "venue": venue,
        "market_id": market_id,
        "title": title,
        "description": "",
        "resolution_rules": "",
        "close_time": None,
        "category": "",
        "outcomes": ("yes", "no"),
        "active": True,
    }
    values.update(changes)
    return Market(**values)  # type: ignore[arg-type]


class CatalogTests(unittest.TestCase):
    def test_parses_canonical_and_venue_native_field_aliases(self) -> None:
        parsed = parse_catalog(
            {
                "markets": [
                    {
                        "ticker": "K-1",
                        "question": "Will Alice win?",
                        "rules": "Official result.",
                        "close_time_unix_ms": 1_841_140_800_000,
                        "outcomes": {"yes": "Yes", "no": "No"},
                    }
                ]
            },
            "kalshi",
        )
        self.assertEqual(parsed[0].market_id, "K-1")
        self.assertEqual(parsed[0].resolution_rules, "Official result.")
        self.assertTrue(parsed[0].is_binary)
        self.assertEqual(parsed[0].close_time, datetime(2028, 5, 5, 12, tzinfo=timezone.utc))

    def test_duplicate_ids_are_rejected(self) -> None:
        payload = [{"id": "same", "title": "A"}, {"id": "same", "title": "B"}]
        with self.assertRaisesRegex(ValueError, "duplicate"):
            parse_catalog(payload, "kalshi")


class BlockingTests(unittest.TestCase):
    def test_incompatible_year_and_threshold_are_blocked(self) -> None:
        left = market("kalshi", "k", "Will Alice win in 2028 with over 50 votes?")
        wrong_year = market("polymarket", "p1", "Will Alice win in 2029 with over 50 votes?")
        wrong_number = market("polymarket", "p2", "Will Alice win in 2028 with over 60 votes?")
        self.assertEqual(block_reason(left, wrong_year), "year")
        self.assertEqual(block_reason(left, wrong_number), "number")

    def test_close_times_outside_tolerance_are_blocked(self) -> None:
        left = market(
            "kalshi", "k", "Will Alice win?", close_time=datetime(2028, 1, 1, tzinfo=timezone.utc)
        )
        right = market(
            "polymarket", "p", "Will Alice win?", close_time=datetime(2028, 2, 1, tzinfo=timezone.utc)
        )
        self.assertEqual(block_reason(left, right, 14), "close_time")


class MatcherTests(unittest.TestCase):
    def setUp(self) -> None:
        self.matcher = Matcher(MatcherConfig(accept_threshold=0.70, ambiguity_margin=0.08))

    def test_accepts_exact_offline_match_and_maps_negation(self) -> None:
        result = self.matcher.match(
            [market("kalshi", "K1", "Will Alice win the 2028 London mayor election?")],
            [market("polymarket", "P1", "Will Alice not win the 2028 London mayor election?")],
        )
        self.assertEqual(len(result.pairs), 1)
        self.assertEqual(result.pairs[0].yes_maps_to, "no")
        self.assertEqual(result.pairs[0].source, "offline_tfidf")

    def test_rejects_ambiguous_equal_candidates(self) -> None:
        title = "Will Alice win the 2028 London mayor election?"
        result = self.matcher.match(
            [market("kalshi", "K1", title)],
            [market("polymarket", "P1", title), market("polymarket", "P2", title)],
        )
        self.assertEqual(result.pairs, ())

    def test_inactive_and_non_binary_markets_are_excluded(self) -> None:
        result = self.matcher.match(
            [market("kalshi", "K1", "Will Alice win?", active=False)],
            [market("polymarket", "P1", "Will Alice win?", outcomes=("Alice", "Bob"))],
        )
        self.assertEqual(result.active_kalshi_count, 0)
        self.assertEqual(result.active_polymarket_count, 0)

    def test_tfidf_matrix_is_symmetric_and_deterministic(self) -> None:
        documents = ["alpha beta gamma", "alpha beta", "unrelated result"]
        first = tfidf_cosine_matrix(documents)
        second = tfidf_cosine_matrix(documents)
        self.assertEqual(first, second)
        self.assertAlmostEqual(first[0][1], first[1][0])
        self.assertEqual(first[0][0], 1.0)

    def test_semantic_backend_must_judge_equivalence_above_threshold(self) -> None:
        class FakeBackend:
            def embeddings(self, documents: list[str]) -> list[list[float]]:
                return [[1.0, 0.0] for _ in documents]

            def judge(self, _kalshi: Market, _polymarket: Market) -> SemanticJudgment:
                return SemanticJudgment(
                    equivalent=True,
                    confidence=0.96,
                    yes_maps_to="yes",
                    mismatches=(),
                    reason="Resolution rules describe the same event.",
                )

        matcher_with_backend = Matcher(
            MatcherConfig(accept_threshold=0.90),
            openai_backend=FakeBackend(),  # type: ignore[arg-type]
        )
        title = "Will Alice win the 2028 London mayor election?"
        result = matcher_with_backend.match(
            [market("kalshi", "K1", title)],
            [market("polymarket", "P1", title)],
        )
        self.assertEqual(result.mode, "openai")
        self.assertEqual(result.pairs[0].source, "openai_judged")
        self.assertEqual(result.pairs[0].confidence, 0.96)


class CacheTests(unittest.TestCase):
    def test_embedding_and_judgment_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with MatcherCache(Path(directory) / "cache.sqlite3") as cache:
                cache.put_embedding("embed", "hash", [0.25, 0.75])
                cache.put_judgment("judge", "pair", {"equivalent": True})
                self.assertEqual(cache.get_embedding("embed", "hash"), [0.25, 0.75])
                self.assertEqual(cache.get_judgment("judge", "pair"), {"equivalent": True})


class CliTests(unittest.TestCase):
    def test_offline_cli_atomically_writes_versioned_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            kalshi_path = root / "kalshi.json"
            polymarket_path = root / "polymarket.json"
            output_path = root / "pairs.json"
            cache_path = root / "cache.sqlite3"
            title = "Will Alice win the 2028 London mayor election?"
            kalshi_path.write_text(json.dumps([{"id": "K1", "title": title}]), encoding="utf-8")
            polymarket_path.write_text(
                json.dumps({"markets": [{"id": "P1", "title": title}]}), encoding="utf-8"
            )

            status = main(
                [
                    "--kalshi-catalog", str(kalshi_path),
                    "--polymarket-catalog", str(polymarket_path),
                    "--output", str(output_path),
                    "--cache", str(cache_path),
                    "--offline",
                ]
            )
            payload = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(status, 0)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["matcher"]["mode"], "offline")
            self.assertEqual(payload["pairs"][0]["kalshi_id"], "K1")
            self.assertEqual(payload["pairs"][0]["polymarket_id"], "P1")
            self.assertEqual(
                payload["pairs"][0]["outcome_mapping"],
                {"kalshi_yes": "polymarket_yes", "kalshi_no": "polymarket_no"},
            )
            self.assertFalse(list(root.glob(".pairs.json.*.tmp")))


if __name__ == "__main__":
    unittest.main()
