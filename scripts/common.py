"""Small shared helpers for repository runtime scripts."""
from __future__ import annotations

import json
import os
import platform
import re
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


def normalize_arch(value: str | None) -> str:
    arch = (value or "x86_64").lower()
    aliases = {"amd64": "x86_64", "aarch64": "arm64"}
    arch = aliases.get(arch, arch)
    if arch not in {"x86_64", "arm64"}:
        raise ValueError(f"unsupported arch: {value}")
    return arch


def image_suffix(arch: str) -> str:
    return f"qemu-{normalize_arch(arch)}"


def default_qemu_binary(arch: str) -> str:
    normalized = normalize_arch(arch)
    return "qemu-system-aarch64" if normalized == "arm64" else "qemu-system-x86_64"


def default_console(arch: str) -> str:
    return "ttyAMA0" if normalize_arch(arch) == "arm64" else "ttyS0"


def result_path(root: Path, relative: str, arch: str) -> Path:
    normalized = normalize_arch(arch)
    path = root / relative
    if normalized == "x86_64":
        return path
    return path.with_name(f"{path.stem}_{normalized}{path.suffix}")


def acceleration(arch: str, kvm: Path = Path("/dev/kvm")) -> str:
    normalized = normalize_arch(arch)
    if normalized == "arm64" and platform.system() == "Darwin" and platform.machine() == "arm64":
        return "hvf"
    if kvm.exists() and os.access(kvm, os.R_OK | os.W_OK):
        return "kvm"
    return "tcg"


def machine_and_cpu_args(arch: str, accel: str) -> list[str]:
    normalized = normalize_arch(arch)
    if normalized == "arm64":
        cpu = "host" if accel in {"kvm", "hvf"} else "cortex-a72"
        return ["-machine", f"virt,accel={accel}", "-cpu", cpu]
    cpu = "host" if accel == "kvm" else "max"
    return ["-machine", f"accel={accel}", "-cpu", cpu]


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


# Matches the timing reports emitted by the guest, e.g.
#   VOGUE-TIMING host-submit[decode]: calls=128 active_ns=900 wait_ns=2400000 bytes=8192
# The label may carry a [phase] suffix; the trailing fields are free-form
# key=value integer counters.
_VOGUE_TIMING_RE = re.compile(r"VOGUE-TIMING\s+(?P<label>\S+):\s+(?P<fields>.+)")


def parse_vogue_timing(text: str) -> dict:
    """Extract VOGUE-TIMING reports from captured guest output.

    Returns a mapping of label (e.g. "host-submit[decode]") to its key=value
    counters. If a label is reported more than once, the last occurrence wins.
    """
    timing: dict[str, dict[str, int]] = {}
    for match in _VOGUE_TIMING_RE.finditer(text):
        fields: dict[str, int] = {}
        for token in match.group("fields").split():
            key, sep, value = token.partition("=")
            if not sep:
                continue
            try:
                fields[key] = int(value)
            except ValueError:
                fields[key] = value
        timing[match.group("label")] = fields
    return timing


def summarize_vogue_profile(timing: dict) -> dict:
    """Derive an active-vs-wait breakdown of the unique guest stack per phase.

    "active" = our translation work (L2 encode + host-submit enqueue/notify
    + ring flush); "wait" = time spinning on the host (host-submit dequeue
    + fence poll). Percentages show where our stack's time actually goes.
    """
    summary: dict[str, dict] = {}
    for phase in ("prompt", "decode"):
        l2 = timing.get(f"L2-submit[{phase}]", {})
        sub = timing.get(f"host-submit[{phase}]", {})
        fence = timing.get(f"fence-wait[{phase}]", {})
        l3 = timing.get(f"L3-flush[{phase}]", {})
        flush = timing.get(f"host-flush[{phase}]", {})
        active = (l2.get("total_ns", 0) + sub.get("active_ns", 0)
                  + l3.get("total_ns", 0))
        # host-flush (uk_venus_submit) is the real batch round-trip in the
        # batched config; it blocks on the host, so count it as wait.
        wait = (sub.get("wait_ns", 0) + fence.get("total_ns", 0)
                + flush.get("total_ns", 0))
        total = active + wait
        if total == 0:
            continue
        summary[phase] = {
            "active_ns": active,
            "wait_ns": wait,
            "active_pct": round(100.0 * active / total, 2),
            "wait_pct": round(100.0 * wait / total, 2),
            "host_flushes": flush.get("calls", 0),
            "host_flush_ns": flush.get("total_ns", 0),
            "host_flush_bytes": flush.get("bytes", 0),
            "roundtrips": sub.get("calls", 0),
        }
    return summary
