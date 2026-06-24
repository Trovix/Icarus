from __future__ import annotations

from dataclasses import dataclass
from datetime import timedelta
from typing import Sequence

from .models import Market, PairMatch
from .openai_backend import OpenAIBackend
from .similarity import (
    cosine,
    extract_numbers,
    extract_years,
    inferred_polarity,
    meaningful_tokens,
    TfidfIndex,
)


@dataclass(frozen=True)
class MatcherConfig:
    candidate_limit: int = 5
    candidate_threshold: float = 0.20
    accept_threshold: float = 0.72
    ambiguity_margin: float = 0.08
    date_tolerance_days: int = 14

    def __post_init__(self) -> None:
        if self.candidate_limit < 1:
            raise ValueError("candidate_limit must be at least 1")
        for name in ("candidate_threshold", "accept_threshold", "ambiguity_margin"):
            value = getattr(self, name)
            if not 0 <= value <= 1:
                raise ValueError(f"{name} must be between 0 and 1")
        if self.date_tolerance_days < 0:
            raise ValueError("date_tolerance_days cannot be negative")


@dataclass(frozen=True)
class Candidate:
    kalshi_index: int
    polymarket_index: int
    lexical_similarity: float
    semantic_similarity: float | None = None

    @property
    def ranking_score(self) -> float:
        if self.semantic_similarity is None:
            return self.lexical_similarity
        return (0.35 * self.lexical_similarity) + (0.65 * self.semantic_similarity)


@dataclass(frozen=True)
class MatchResult:
    pairs: tuple[PairMatch, ...]
    candidate_count: int
    active_kalshi_count: int
    active_polymarket_count: int
    mode: str


class Matcher:
    def __init__(
        self,
        config: MatcherConfig | None = None,
        openai_backend: OpenAIBackend | None = None,
    ) -> None:
        self.config = config or MatcherConfig()
        self.openai_backend = openai_backend

    def match(self, kalshi: Sequence[Market], polymarket: Sequence[Market]) -> MatchResult:
        left = [market for market in kalshi if market.active and market.is_binary]
        right = [market for market in polymarket if market.active and market.is_binary]
        candidates = self._rank_candidates(left, right)
        mode = "openai" if self.openai_backend else "offline"
        accepted: list[PairMatch] = []

        for candidate in self._unambiguous_mutual_bests(candidates):
            kalshi_market = left[candidate.kalshi_index]
            polymarket_market = right[candidate.polymarket_index]
            if self.openai_backend:
                judgment = self.openai_backend.judge(kalshi_market, polymarket_market)
                if not judgment.equivalent or judgment.confidence < self.config.accept_threshold:
                    continue
                accepted.append(
                    PairMatch(
                        kalshi_id=kalshi_market.market_id,
                        polymarket_id=polymarket_market.market_id,
                        kalshi_title=kalshi_market.title,
                        polymarket_title=polymarket_market.title,
                        yes_maps_to=judgment.yes_maps_to,
                        confidence=judgment.confidence,
                        lexical_similarity=candidate.lexical_similarity,
                        semantic_similarity=candidate.semantic_similarity,
                        source="openai_judged",
                        reason=judgment.reason,
                        mismatches=judgment.mismatches,
                    )
                )
            elif candidate.ranking_score >= self.config.accept_threshold:
                accepted.append(
                    PairMatch(
                        kalshi_id=kalshi_market.market_id,
                        polymarket_id=polymarket_market.market_id,
                        kalshi_title=kalshi_market.title,
                        polymarket_title=polymarket_market.title,
                        yes_maps_to=inferred_polarity(kalshi_market.title, polymarket_market.title),
                        confidence=candidate.ranking_score,
                        lexical_similarity=candidate.lexical_similarity,
                        semantic_similarity=None,
                        source="offline_tfidf",
                        reason="High-confidence deterministic TF-IDF match after contract blocking.",
                    )
                )

        accepted.sort(key=lambda pair: (pair.kalshi_id, pair.polymarket_id))
        return MatchResult(
            pairs=tuple(accepted),
            candidate_count=len(candidates),
            active_kalshi_count=len(left),
            active_polymarket_count=len(right),
            mode=mode,
        )

    def _rank_candidates(
        self, kalshi: Sequence[Market], polymarket: Sequence[Market]
    ) -> list[Candidate]:
        if not kalshi or not polymarket:
            return []
        documents = [market.document for market in kalshi] + [market.document for market in polymarket]
        lexical_index = TfidfIndex(documents)
        right_token_index: dict[str, list[int]] = {}
        for right_index, market in enumerate(polymarket):
            for token in meaningful_tokens(market.title):
                right_token_index.setdefault(token, []).append(right_index)

        candidates: list[Candidate] = []
        offset = len(kalshi)
        for left_index, left_market in enumerate(kalshi):
            row: list[Candidate] = []
            shared_tokens = [
                token for token in meaningful_tokens(left_market.title) if token in right_token_index
            ]
            # Rarest shared title tokens are the strongest deterministic blocking keys.
            shared_tokens.sort(key=lambda token: (len(right_token_index[token]), token))
            possible_rights: set[int] = set()
            for token in shared_tokens[:5]:
                possible_rights.update(right_token_index[token])
            for right_index in sorted(possible_rights):
                right_market = polymarket[right_index]
                if block_reason(left_market, right_market, self.config.date_tolerance_days):
                    continue
                lexical = lexical_index.similarity(left_index, offset + right_index)
                candidate = Candidate(left_index, right_index, lexical)
                if lexical >= self.config.candidate_threshold:
                    row.append(candidate)
            row.sort(key=lambda item: (-item.ranking_score, polymarket[item.polymarket_index].market_id))
            prefilter_limit = self.config.candidate_limit * 4 if self.openai_backend else self.config.candidate_limit
            candidates.extend(row[:prefilter_limit])

        if not self.openai_backend or not candidates:
            return candidates

        relevant_document_indexes = sorted(
            {candidate.kalshi_index for candidate in candidates}
            | {offset + candidate.polymarket_index for candidate in candidates}
        )
        relevant_vectors = self.openai_backend.embeddings(
            [documents[index] for index in relevant_document_indexes]
        )
        if len(relevant_vectors) != len(relevant_document_indexes):
            raise RuntimeError("embedding API returned an unexpected number of vectors")
        vector_by_index = dict(zip(relevant_document_indexes, relevant_vectors))
        reranked = [
            Candidate(
                candidate.kalshi_index,
                candidate.polymarket_index,
                candidate.lexical_similarity,
                cosine(
                    vector_by_index[candidate.kalshi_index],
                    vector_by_index[offset + candidate.polymarket_index],
                ),
            )
            for candidate in candidates
        ]
        limited: list[Candidate] = []
        for left_index in range(len(kalshi)):
            row = [candidate for candidate in reranked if candidate.kalshi_index == left_index]
            row.sort(key=lambda item: (-item.ranking_score, polymarket[item.polymarket_index].market_id))
            limited.extend(row[: self.config.candidate_limit])
        return limited

    def _unambiguous_mutual_bests(self, candidates: Sequence[Candidate]) -> list[Candidate]:
        by_left: dict[int, list[Candidate]] = {}
        by_right: dict[int, list[Candidate]] = {}
        for candidate in candidates:
            by_left.setdefault(candidate.kalshi_index, []).append(candidate)
            by_right.setdefault(candidate.polymarket_index, []).append(candidate)
        for values in (*by_left.values(), *by_right.values()):
            values.sort(key=lambda item: (-item.ranking_score, item.kalshi_index, item.polymarket_index))

        accepted: list[Candidate] = []
        for left_index in sorted(by_left):
            left_ranked = by_left[left_index]
            best = left_ranked[0]
            right_ranked = by_right[best.polymarket_index]
            if right_ranked[0] != best:
                continue
            left_runner_up = left_ranked[1].ranking_score if len(left_ranked) > 1 else 0.0
            right_runner_up = right_ranked[1].ranking_score if len(right_ranked) > 1 else 0.0
            if best.ranking_score - max(left_runner_up, right_runner_up) < self.config.ambiguity_margin:
                continue
            accepted.append(best)
        return accepted


def block_reason(left: Market, right: Market, date_tolerance_days: int = 14) -> str | None:
    if not left.active or not right.active:
        return "inactive"
    if not left.is_binary or not right.is_binary:
        return "non_binary"
    if left.close_time and right.close_time:
        if abs(left.close_time - right.close_time) > timedelta(days=date_tolerance_days):
            return "close_time"

    left_years = extract_years(left.title)
    right_years = extract_years(right.title)
    if left_years and right_years and left_years.isdisjoint(right_years):
        return "year"
    left_numbers = extract_numbers(left.title)
    right_numbers = extract_numbers(right.title)
    if left_numbers and right_numbers and left_numbers != right_numbers:
        return "number"

    left_tokens = meaningful_tokens(left.title)
    right_tokens = meaningful_tokens(right.title)
    if left_tokens and right_tokens and not left_tokens.intersection(right_tokens):
        return "no_title_overlap"
    return None
