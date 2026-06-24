from __future__ import annotations

import math
import re
from collections import Counter
from typing import Any, Sequence


TOKEN_RE = re.compile(r"[a-z0-9]+")
YEAR_RE = re.compile(r"\b(?:19|20)\d{2}\b")
NUMBER_RE = re.compile(r"\b\d+(?:\.\d+)?\b")
NEGATION_RE = re.compile(r"\b(?:not|never|no|won't|wouldn't|isn't|doesn't|fail(?:s|ed)?\s+to)\b")
STOP_WORDS = {
    "a", "an", "and", "are", "as", "at", "be", "before", "by", "did", "do", "does",
    "for", "from", "has", "have", "in", "is", "it", "market", "of", "on", "or", "the",
    "this", "to", "was", "will", "with", "would", "yes",
}


def tokenize(text: str, *, ngrams: bool = True) -> list[str]:
    words = [token for token in TOKEN_RE.findall(text.lower()) if token not in STOP_WORDS]
    if not ngrams:
        return words
    return words + [f"{left}__{right}" for left, right in zip(words, words[1:])]


def meaningful_tokens(text: str) -> set[str]:
    return {token for token in tokenize(text, ngrams=False) if len(token) > 2}


def extract_years(text: str) -> set[str]:
    return set(YEAR_RE.findall(text))


def extract_numbers(text: str) -> set[str]:
    years = extract_years(text)
    return {number for number in NUMBER_RE.findall(text) if number not in years}


def inferred_polarity(left_title: str, right_title: str) -> str:
    left_negative = bool(NEGATION_RE.search(left_title.lower()))
    right_negative = bool(NEGATION_RE.search(right_title.lower()))
    return "no" if left_negative != right_negative else "yes"


def tfidf_cosine_matrix(documents: Sequence[str]) -> list[list[float]]:
    """Return deterministic TF-IDF cosine similarities.

    scikit-learn is used when installed. The stdlib implementation is intentionally
    equivalent enough for a no-dependency offline run and for bootstrap testing.
    """
    index = TfidfIndex(documents)
    return [
        [index.similarity(left, right) for right in range(len(documents))]
        for left in range(len(documents))
    ]


def cosine(left: Sequence[float], right: Sequence[float]) -> float:
    if len(left) != len(right):
        raise ValueError("vectors must have equal dimensions")
    left_norm = math.sqrt(sum(value * value for value in left))
    right_norm = math.sqrt(sum(value * value for value in right))
    if not left_norm or not right_norm:
        return 0.0
    return sum(a * b for a, b in zip(left, right)) / (left_norm * right_norm)


class TfidfIndex:
    """Sparse TF-IDF index that never materializes an all-document similarity matrix."""

    def __init__(self, documents: Sequence[str]) -> None:
        self._sklearn_matrix: Any | None = None
        self._vectors: list[dict[str, float]] | None = None
        if not documents:
            self._vectors = []
            return
        try:
            from sklearn.feature_extraction.text import TfidfVectorizer  # type: ignore

            vectorizer = TfidfVectorizer(
                tokenizer=lambda value: tokenize(value, ngrams=True),
                token_pattern=None,
                lowercase=False,
                norm="l2",
            )
            self._sklearn_matrix = vectorizer.fit_transform(documents)
        except (ImportError, ValueError):
            self._vectors = _stdlib_tfidf_vectors(documents)

    def similarity(self, left_index: int, right_index: int) -> float:
        if self._sklearn_matrix is not None:
            left = self._sklearn_matrix.getrow(left_index)
            right = self._sklearn_matrix.getrow(right_index)
            return float(left.multiply(right).sum())
        if self._vectors is None:
            return 0.0
        left = self._vectors[left_index]
        right = self._vectors[right_index]
        scan, lookup = (left, right) if len(left) <= len(right) else (right, left)
        return sum(value * lookup.get(token, 0.0) for token, value in scan.items())


def _stdlib_tfidf_vectors(documents: Sequence[str]) -> list[dict[str, float]]:
    tokenized = [tokenize(document, ngrams=True) for document in documents]
    doc_count = len(tokenized)
    document_frequency: Counter[str] = Counter()
    for tokens in tokenized:
        document_frequency.update(set(tokens))

    vectors: list[dict[str, float]] = []
    for tokens in tokenized:
        counts = Counter(tokens)
        vector = {
            token: count * (math.log((1 + doc_count) / (1 + document_frequency[token])) + 1)
            for token, count in counts.items()
        }
        norm = math.sqrt(sum(value * value for value in vector.values()))
        vectors.append({token: value / norm for token, value in vector.items()} if norm else {})
    return vectors
