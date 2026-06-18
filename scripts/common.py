"""Small shared helpers for repository runtime scripts."""
from __future__ import annotations

import json
import os
import platform
import resource
import shlex
import shutil
import subprocess
import threading
import time
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


# ---------------------------------------------------------------------------
# Footprint helpers
# ---------------------------------------------------------------------------

def peak_child_rss_kb() -> int | None:
    """Peak resident memory (KiB) of reaped child processes.

    Reads ``getrusage(RUSAGE_CHILDREN).ru_maxrss`` (kilobytes on Linux), which
    the kernel reports as the high-water RSS of the largest child this process
    has waited for. The runners spawn a single child per invocation (the QEMU
    VM or the native benchmark), so this is that child's peak host memory.
    Returns None if unavailable.
    """
    try:
        kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    except (ValueError, OSError):
        return None
    return int(kb) if kb and kb > 0 else None


def file_size(path: str | Path | None) -> int | None:
    """Return the size of *path* in bytes, or None if it does not exist."""
    if not path:
        return None
    p = Path(path)
    return p.stat().st_size if p.is_file() else None


# ---------------------------------------------------------------------------
# Boot-timed process execution
# ---------------------------------------------------------------------------

class TimedRun:
    """Outcome of :func:`run_timed`: captured output plus marker timings.

    ``marker_times`` maps each marker name that appeared to the wall-clock
    seconds (from just before launch) at which its first matching line was
    read off the serial console. ``peak_rss_kb`` is the peak host RSS (KiB) of
    the launched process (see :func:`peak_child_rss_kb`).
    """

    __slots__ = ("returncode", "text", "timed_out", "marker_times", "peak_rss_kb")

    def __init__(self, returncode: int | None, text: str, timed_out: bool,
                 marker_times: dict[str, float],
                 peak_rss_kb: int | None = None) -> None:
        self.returncode = returncode
        self.text = text
        self.timed_out = timed_out
        self.marker_times = marker_times
        self.peak_rss_kb = peak_rss_kb

    def elapsed(self, name: str) -> float | None:
        """Seconds until the *name* marker first appeared, or None if never."""
        return self.marker_times.get(name)


def run_timed(
    command: list[str],
    *,
    cwd: str | Path | None = None,
    timeout: float,
    markers: dict[str, str] | None = None,
) -> TimedRun:
    """Run *command*, capturing merged stdout/stderr, timing serial markers.

    Unlike ``subprocess.run``, output is streamed on a reader thread so the
    moment each *marker* substring first appears can be timestamped against a
    monotonic clock started just before launch. This is how boot/ready time is
    measured: e.g. ``markers={"boot": "guest booted"}`` yields the wall-clock
    delay from QEMU launch to the guest printing that line. Matching is
    case-insensitive. On timeout the process is killed and whatever was
    captured so far is returned with ``timed_out=True``.
    """
    needles = {name: sub.lower() for name, sub in (markers or {}).items()}
    chunks: list[str] = []
    marker_times: dict[str, float] = {}
    start = time.monotonic()
    proc = subprocess.Popen(
        command, cwd=cwd, text=True, bufsize=1,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )

    def pump() -> None:
        assert proc.stdout is not None
        for line in proc.stdout:
            elapsed = time.monotonic() - start
            chunks.append(line)
            low = line.lower()
            for name, needle in needles.items():
                if name not in marker_times and needle in low:
                    marker_times[name] = round(elapsed, 3)

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()

    timed_out = False
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        proc.wait()
    reader.join(timeout=5)
    if proc.stdout is not None:
        proc.stdout.close()
    # The child is reaped by now, so getrusage reports its peak RSS.
    return TimedRun(proc.returncode, "".join(chunks), timed_out, marker_times,
                    peak_child_rss_kb())
