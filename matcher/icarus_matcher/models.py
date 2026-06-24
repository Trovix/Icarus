from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Iterable, Literal


Venue = Literal["kalshi", "polymarket"]
Polarity = Literal["yes", "no"]


@dataclass(frozen=True)
class Market:
    venue: Venue
    market_id: str
    title: str
    description: str = ""
    resolution_rules: str = ""
    close_time: datetime | None = None
    category: str = ""
    outcomes: tuple[str, ...] = ("yes", "no")
    active: bool = True

    @property
    def document(self) -> str:
        parts = (self.title, self.description, self.resolution_rules, self.category)
        return "\n".join(part.strip() for part in parts if part and part.strip())

    @property
    def is_binary(self) -> bool:
        normalized = {outcome.strip().lower() for outcome in self.outcomes}
        return normalized == {"yes", "no"}


@dataclass(frozen=True)
class PairMatch:
    kalshi_id: str
    polymarket_id: str
    kalshi_title: str
    polymarket_title: str
    yes_maps_to: Polarity
    confidence: float
    lexical_similarity: float
    semantic_similarity: float | None
    source: Literal["offline_tfidf", "openai_judged"]
    reason: str
    mismatches: tuple[str, ...] = field(default_factory=tuple)

    def to_dict(self) -> dict[str, Any]:
        same_polarity = self.yes_maps_to == "yes"
        return {
            "kalshi_id": self.kalshi_id,
            "polymarket_id": self.polymarket_id,
            "kalshi_title": self.kalshi_title,
            "polymarket_title": self.polymarket_title,
            "yes_maps_to": self.yes_maps_to,
            "outcome_mapping": {
                "kalshi_yes": "polymarket_yes" if same_polarity else "polymarket_no",
                "kalshi_no": "polymarket_no" if same_polarity else "polymarket_yes",
            },
            "confidence": round(self.confidence, 6),
            "lexical_similarity": round(self.lexical_similarity, 6),
            "semantic_similarity": (
                None if self.semantic_similarity is None else round(self.semantic_similarity, 6)
            ),
            "source": self.source,
            "reason": self.reason,
            "mismatches": list(self.mismatches),
        }


def parse_catalog(payload: Any, venue: Venue) -> list[Market]:
    records: Any
    if isinstance(payload, list):
        records = payload
    elif isinstance(payload, dict):
        records = payload.get("markets", payload.get("data"))
    else:
        records = None

    if not isinstance(records, list):
        raise ValueError("catalog must be a JSON array or an object containing a 'markets' array")

    markets: list[Market] = []
    seen: set[str] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError(f"catalog market at index {index} must be an object")
        market = _parse_market(record, venue, index)
        if market.market_id in seen:
            raise ValueError(f"duplicate {venue} market id: {market.market_id}")
        seen.add(market.market_id)
        markets.append(market)
    return markets


def _parse_market(record: dict[str, Any], venue: Venue, index: int) -> Market:
    market_id = _first_string(
        record,
        ("venue_market_id", "market_id", "id", "ticker", "condition_id", "conditionId"),
    )
    title = _first_string(record, ("title", "question", "name"))
    if not market_id:
        raise ValueError(f"{venue} market at index {index} has no id")
    if not title:
        raise ValueError(f"{venue} market {market_id!r} has no title")

    status = str(record.get("status", "")).strip().lower()
    inactive_statuses = {"closed", "resolved", "settled", "inactive", "cancelled", "canceled"}
    active_value = record.get("active", record.get("is_active", True))
    active = _as_bool(active_value) and status not in inactive_statuses

    outcomes_value = record.get("outcomes", ("yes", "no"))
    outcomes = _parse_outcomes(outcomes_value)
    close_value = _first_value(
        record,
        (
            "close_time",
            "closeTime",
            "close_time_unix_ms",
            "end_date",
            "endDate",
            "expiration_time",
            "expirationTime",
        ),
    )

    return Market(
        venue=venue,
        market_id=market_id,
        title=title,
        description=_first_string(record, ("description", "subtitle")),
        resolution_rules=_first_string(
            record,
            ("resolution_rules", "resolutionRules", "rules", "resolution_source"),
        ),
        close_time=_parse_datetime(close_value),
        category=_first_string(record, ("category", "series", "event_category")),
        outcomes=outcomes,
        active=active,
    )


def _first_value(record: dict[str, Any], keys: Iterable[str]) -> Any:
    for key in keys:
        if key in record and record[key] is not None:
            return record[key]
    return None


def _first_string(record: dict[str, Any], keys: Iterable[str]) -> str:
    value = _first_value(record, keys)
    return value.strip() if isinstance(value, str) else ""


def _parse_outcomes(value: Any) -> tuple[str, ...]:
    if isinstance(value, str):
        return tuple(part.strip() for part in value.split(",") if part.strip())
    if isinstance(value, dict):
        return tuple(str(part).strip() for part in value.keys() if str(part).strip())
    if isinstance(value, list) or isinstance(value, tuple):
        parsed: list[str] = []
        for part in value:
            if isinstance(part, dict):
                label = part.get("name", part.get("label", part.get("title", "")))
                if label:
                    parsed.append(str(label).strip())
            elif str(part).strip():
                parsed.append(str(part).strip())
        return tuple(parsed)
    return ()


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() not in {"false", "0", "no", "off", "inactive"}
    return bool(value)


def _parse_datetime(value: Any) -> datetime | None:
    if value is None or value == "":
        return None
    if isinstance(value, (int, float)):
        timestamp = float(value)
        if timestamp > 10_000_000_000:
            timestamp /= 1000
        return datetime.fromtimestamp(timestamp, tz=timezone.utc)
    if not isinstance(value, str):
        raise ValueError(f"unsupported close time value: {value!r}")
    normalized = value.strip()
    if normalized.endswith("Z"):
        normalized = normalized[:-1] + "+00:00"
    parsed = datetime.fromisoformat(normalized)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)
