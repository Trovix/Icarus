from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any, Sequence


class MatcherCache:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path)
        self.connection.execute("PRAGMA journal_mode=WAL")
        self.connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS embeddings (
                model TEXT NOT NULL,
                content_hash TEXT NOT NULL,
                vector_json TEXT NOT NULL,
                PRIMARY KEY (model, content_hash)
            );
            CREATE TABLE IF NOT EXISTS judgments (
                model TEXT NOT NULL,
                pair_hash TEXT NOT NULL,
                judgment_json TEXT NOT NULL,
                PRIMARY KEY (model, pair_hash)
            );
            """
        )

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> MatcherCache:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def get_embedding(self, model: str, content_hash: str) -> list[float] | None:
        row = self.connection.execute(
            "SELECT vector_json FROM embeddings WHERE model = ? AND content_hash = ?",
            (model, content_hash),
        ).fetchone()
        return None if row is None else [float(value) for value in json.loads(row[0])]

    def put_embedding(self, model: str, content_hash: str, vector: Sequence[float]) -> None:
        self.connection.execute(
            "INSERT OR REPLACE INTO embeddings(model, content_hash, vector_json) VALUES (?, ?, ?)",
            (model, content_hash, json.dumps(list(vector), separators=(",", ":"))),
        )
        self.connection.commit()
    def get_judgment(self, model: str, pair_hash: str) -> dict[str, Any] | None:
        row = self.connection.execute(
            "SELECT judgment_json FROM judgments WHERE model = ? AND pair_hash = ?",
            (model, pair_hash),
        ).fetchone()
        return None if row is None else json.loads(row[0])

    def put_judgment(self, model: str, pair_hash: str, judgment: dict[str, Any]) -> None:
        self.connection.execute(
            "INSERT OR REPLACE INTO judgments(model, pair_hash, judgment_json) VALUES (?, ?, ?)",
            (model, pair_hash, json.dumps(judgment, sort_keys=True, separators=(",", ":"))),
        )
        self.connection.commit()
