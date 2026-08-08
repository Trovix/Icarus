# ICARUS market matcher

Candidate generation is offline-first. It deterministically blocks incompatible
contracts, ranks the remaining cross-venue candidates with TF-IDF cosine similarity,
and only accepts mutual-best matches that clear both a confidence threshold and an
ambiguity margin. If `OPENAI_API_KEY` is present, embeddings rerank candidates and
`gpt-5.6-terra` conservatively judges the full contract text using structured output.
The autonomous C++ app accepts semantic `openai_judged` output by default; offline
acceptance must be enabled explicitly in `config/paper.json`.

Install from the repository root:

```powershell
py -m pip install -e ".[all,test]"
```

Run without API calls:

```powershell
python -m icarus_matcher `
  --kalshi-catalog data/kalshi_markets.json `
  --polymarket-catalog data/polymarket_markets.json `
  --output data/market_pairs.json `
  --offline
```

Omit `--offline` to enable AI matching when `OPENAI_API_KEY` is available. API
results are cached in SQLite by content hash, model, and prompt version.

## Catalog input

Each catalog may be either a JSON array or `{ "markets": [...] }`. The canonical
fields are:

```json
{
  "venue_market_id": "venue-specific-id",
  "title": "Will the event happen?",
  "description": "Contract details",
  "resolution_rules": "Exact settlement conditions",
  "close_time": "2028-05-01T12:00:00Z",
  "category": "Politics",
  "outcomes": ["Yes", "No"],
  "active": true
}
```

Common venue-native aliases such as `ticker`, `condition_id`, `question`, `rules`,
`close_time_unix_ms`, and `endDate` are accepted. Missing outcomes default to `Yes`/`No`. Closed,
resolved, inactive, and non-binary markets are excluded.

## Pair output contract

The output is atomically replaced and has this stable versioned envelope:

```json
{
  "schema_version": 1,
  "generated_at": "2026-08-11T00:00:00+00:00",
  "matcher": {
    "mode": "offline",
    "candidate_count": 1,
    "active_market_counts": {"kalshi": 1, "polymarket": 1}
  },
  "pairs": [
    {
      "kalshi_id": "K-1",
      "polymarket_id": "P-1",
      "kalshi_title": "Will the event happen?",
      "polymarket_title": "Will the event happen?",
      "yes_maps_to": "yes",
      "outcome_mapping": {
        "kalshi_yes": "polymarket_yes",
        "kalshi_no": "polymarket_no"
      },
      "confidence": 1.0,
      "lexical_similarity": 1.0,
      "semantic_similarity": null,
      "source": "offline_tfidf",
      "reason": "High-confidence deterministic TF-IDF match after contract blocking.",
      "mismatches": []
    }
  ]
}
```

The C++ loader should require `schema_version == 1`, read the `pairs` array, and
reject unknown polarity values. `yes_maps_to` always means the Polymarket outcome
corresponding to Kalshi YES; `outcome_mapping` spells out both directions for spread
normalization. The two original ID fields remain mandatory.

The core matcher has no mandatory third-party dependency. If scikit-learn is
installed, it accelerates TF-IDF; otherwise a deterministic standard-library
implementation is used. The OpenAI path requires the `ai` optional dependencies.
