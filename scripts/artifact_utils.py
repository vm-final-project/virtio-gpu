#!/usr/bin/env python3
"""Shared helpers for canonical JSON-only evidence artifacts."""
from __future__ import annotations

import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def metadata(source: str, *, version: int = 1) -> dict[str, Any]:
    return {
        "generated_utc": utc_now(),
        "source": source,
        "version": version,
    }


def block(
    reason: str,
    *,
    stage: str,
    first_missing_dependency: str | None = None,
    claim_allowed: str = "",
    claim_forbidden: str = "",
    next_step: str = "",
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "reason": reason,
        "stage": stage,
        "claim_allowed": claim_allowed,
        "claim_forbidden": claim_forbidden,
        "next_step": next_step,
    }
    if first_missing_dependency:
        payload["first_missing_dependency"] = first_missing_dependency
    return payload


def make_artifact(
    *,
    source: str,
    status: str,
    headline: str,
    counts: dict[str, Any] | None = None,
    artifacts: dict[str, Any] | None = None,
    checks: list[dict[str, Any]] | None = None,
    rows: list[dict[str, Any]] | None = None,
    block_payload: dict[str, Any] | None = None,
    extra: dict[str, Any] | None = None,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "metadata": metadata(source),
        "status": status,
        "summary": {
            "headline": headline,
            "counts": counts or {},
        },
        "artifacts": artifacts or {},
        "checks": checks or [],
        "rows": rows or [],
    }
    if status.startswith("blocked:") and block_payload:
        payload["block"] = block_payload
    if extra:
        payload.update(extra)
    return payload


def blocked_artifact(
    *,
    source: str,
    status: str,
    headline: str,
    stage: str,
    first_missing_dependency: str | None = None,
    claim_allowed: str,
    claim_forbidden: str,
    next_step: str,
    artifacts: dict[str, Any] | None = None,
    checks: list[dict[str, Any]] | None = None,
    rows: list[dict[str, Any]] | None = None,
    counts: dict[str, Any] | None = None,
    extra: dict[str, Any] | None = None,
) -> dict[str, Any]:
    return make_artifact(
        source=source,
        status=status,
        headline=headline,
        counts=counts,
        artifacts=artifacts,
        checks=checks,
        rows=rows,
        block_payload=block(
            status,
            stage=stage,
            first_missing_dependency=first_missing_dependency,
            claim_allowed=claim_allowed,
            claim_forbidden=claim_forbidden,
            next_step=next_step,
        ),
        extra=extra,
    )


def write_json(path: Path, payload: dict[str, Any]) -> dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")
    return payload


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def load_required_json(
    path: Path,
    *,
    source: str,
    headline: str,
    missing_status: str,
    bad_json_status: str,
    next_step: str,
) -> dict[str, Any]:
    if not path.exists():
        return blocked_artifact(
            source=source,
            status=missing_status,
            headline=headline,
            stage="artifact-missing",
            first_missing_dependency=str(path),
            claim_allowed="Artifact blocker recorded; no runtime claim.",
            claim_forbidden="Passing runtime claim without the required upstream JSON artifact.",
            next_step=next_step,
            artifacts={"required_json": str(path)},
        )
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return blocked_artifact(
            source=source,
            status=bad_json_status,
            headline=headline,
            stage="validation",
            first_missing_dependency=str(path),
            claim_allowed="Bad JSON blocker recorded; no runtime claim.",
            claim_forbidden="Passing runtime claim while the upstream JSON artifact is malformed.",
            next_step=next_step,
            artifacts={"required_json": str(path)},
        )


def rows_by_status(rows: list[dict[str, Any]]) -> dict[str, int]:
    return dict(sorted(Counter(str(row.get("status", "missing")) for row in rows).items()))
