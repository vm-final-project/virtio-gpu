"""Small shared helpers for repository runtime scripts."""
from __future__ import annotations

import json
import os
import shlex
import shutil
from datetime import datetime, timezone
from pathlib import Path


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def resolve_model(path: str | Path | None) -> Path | None:
    candidate = Path(path) if path else None
    return candidate if candidate and candidate.is_file() else None


def resolve_qemu(value: str) -> str | None:
    found = shutil.which(value)
    if found:
        return found
    return value if Path(value).is_file() else None


def acceleration(kvm: Path = Path("/dev/kvm")) -> tuple[str, str]:
    if kvm.exists() and os.access(kvm, os.R_OK | os.W_OK):
        return "kvm", "host"
    return "tcg", "max"


def result(
    status: str,
    command: list[str] | str = "",
    *,
    inputs: dict | None = None,
    metrics: dict | None = None,
    error: str | None = None,
) -> dict:
    return {
        "status": status,
        "generated_utc": utc_now(),
        "command": shlex.join(command) if isinstance(command, list) else command,
        "inputs": inputs or {},
        "metrics": metrics or {},
        "error": error,
    }


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def decode(value: str | bytes | None) -> str:
    if value is None:
        return ""
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value
