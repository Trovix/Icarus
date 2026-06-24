from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from typing import Any, Literal, Sequence

from .cache import MatcherCache
from .models import Market


PROMPT_VERSION = "market-equivalence-v1"


@dataclass(frozen=True)
class SemanticJudgment:
    equivalent: bool
    confidence: float
    yes_maps_to: Literal["yes", "no"]
    mismatches: tuple[str, ...]
    reason: str


class OpenAIBackend:
    def __init__(
        self,
        cache: MatcherCache,
        *,
        embedding_model: str = "text-embedding-3-large",
        judge_model: str = "gpt-5.6-terra",
    ) -> None:
        try:
            from openai import OpenAI
        except ImportError as error:
            raise RuntimeError("OpenAI mode requires: pip install -e '.[ai]'") from error
        self.client = OpenAI()
        self.cache = cache
        self.embedding_model = embedding_model
        self.judge_model = judge_model

    def embeddings(self, documents: Sequence[str]) -> list[list[float]]:
        hashes = [_digest(document) for document in documents]
        result: list[list[float] | None] = [
            self.cache.get_embedding(self.embedding_model, digest) for digest in hashes
        ]
        missing_indexes = [index for index, vector in enumerate(result) if vector is None]
        if missing_indexes:
            response = self.client.embeddings.create(
                model=self.embedding_model,
                input=[documents[index] for index in missing_indexes],
                encoding_format="float",
            )
            ordered = sorted(response.data, key=lambda item: item.index)
            for index, item in zip(missing_indexes, ordered):
                vector = [float(value) for value in item.embedding]
                result[index] = vector
                self.cache.put_embedding(self.embedding_model, hashes[index], vector)
        return [vector for vector in result if vector is not None]

    def judge(self, kalshi: Market, polymarket: Market) -> SemanticJudgment:
        payload = {
            "prompt_version": PROMPT_VERSION,
            "kalshi": _market_payload(kalshi),
            "polymarket": _market_payload(polymarket),
        }
        pair_hash = _digest(json.dumps(payload, sort_keys=True, separators=(",", ":")))
        cached = self.cache.get_judgment(self.judge_model, pair_hash)
        if cached is not None:
            return _judgment_from_mapping(cached)

        try:
            from pydantic import BaseModel, ConfigDict, Field
        except ImportError as error:
            raise RuntimeError("Structured judging requires: pip install -e '.[ai]'") from error

        class JudgmentSchema(BaseModel):
            model_config = ConfigDict(extra="forbid")
            equivalent: bool
            confidence: float = Field(ge=0.0, le=1.0)
            yes_maps_to: Literal["yes", "no"]
            mismatches: list[str]
            reason: str

        response = self.client.responses.parse(
            model=self.judge_model,
            input=[
                {
                    "role": "system",
                    "content": (
                        "You compare prediction-market contracts. Decide whether both contracts "
                        "represent the same exhaustive event under compatible resolution rules. "
                        "Dates, thresholds, entities, data sources, cancellation rules, and wording "
                        "must agree. yes_maps_to means the Polymarket outcome corresponding to "
                        "Kalshi YES: set it to 'no' only when Kalshi YES is Polymarket NO. "
                        "Be conservative."
                    ),
                },
                {"role": "user", "content": json.dumps(payload, sort_keys=True)},
            ],
            text_format=JudgmentSchema,
        )
        parsed = response.output_parsed
        if parsed is None:
            raise RuntimeError("semantic judge returned no structured result")
        mapping = parsed.model_dump()
        self.cache.put_judgment(self.judge_model, pair_hash, mapping)
        return _judgment_from_mapping(mapping)


def _market_payload(market: Market) -> dict[str, Any]:
    return {
        "id": market.market_id,
        "title": market.title,
        "description": market.description,
        "resolution_rules": market.resolution_rules,
        "close_time": market.close_time.isoformat() if market.close_time else None,
        "category": market.category,
        "outcomes": list(market.outcomes),
    }


def _judgment_from_mapping(value: dict[str, Any]) -> SemanticJudgment:
    return SemanticJudgment(
        equivalent=bool(value["equivalent"]),
        confidence=max(0.0, min(1.0, float(value["confidence"]))),
        yes_maps_to="no" if value.get("yes_maps_to") == "no" else "yes",
        mismatches=tuple(str(item) for item in value.get("mismatches", [])),
        reason=str(value.get("reason", "")),
    )


def _digest(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()
