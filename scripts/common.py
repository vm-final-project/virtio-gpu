"""Small shared helpers for repository runtime scripts."""
from __future__ import annotations

import json
import os
import platform
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


def smp_args(count: int) -> list[str]:
    """QEMU -smp fragment for booting `count` guest vCPUs.

    count <= 1 returns [] so the historical single-vCPU command (and the
    baselines captured with it) is preserved byte-for-byte unless SMP is
    explicitly requested. The guest must additionally be built with
    CONFIG_UKPLAT_CPU_MAXCOUNT >= count for the extra vCPUs to come online.
    """
    if count <= 1:
        return []
    return ["-smp", str(count)]


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
