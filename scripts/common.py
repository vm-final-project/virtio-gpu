"""Small shared helpers for repository runtime scripts."""
from __future__ import annotations

import json
import os
import platform
import resource
import re
import shlex
import shutil
import socket
import subprocess
import threading
import time
import urllib.request
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
    """Non-overlapping active-vs-wait split of the unique guest stack per phase.

    The raw counters NEST, so they cannot be summed naively:
      * host-flush (uk_venus_submit) wraps host-submit (cmd_submit), so
        host-flush.total already includes host-submit's active+wait.
      * vk-encode is pure serialization (the batch auto-flush round-trip is
        excluded at the source; see UK_ENC_SUBMIT).
      * L2-submit re-times the vkQueueSubmit command whose encode is already
        inside vk-encode, so it is omitted here to avoid double-counting.

    Decomposition per phase (each nanosecond attributed once):
      encode       = vk-encode                    (L2 serialization, guest)
      roundtrip    = host-flush (+ L3-flush ring) (one SUBMIT_3D, wraps L4)
        host_wait  = host-submit.wait (+ fence)   (blocked on host/GPU)
        guest_sub  = roundtrip - host_wait        (sglist + notify + L3 frame)
      active (ours)= encode + guest_sub
      wait  (host) = host_wait

    The model's own CPU time (ggml graph build / sampling between dispatches)
    is NOT counted here; it is the wall-clock residual outside these counters.
    """
    summary: dict[str, dict] = {}
    for phase in ("prompt", "decode"):
        enc = timing.get(f"vk-encode[{phase}]", {})
        sub = timing.get(f"host-submit[{phase}]", {})
        fence = timing.get(f"fence-wait[{phase}]", {})
        l3 = timing.get(f"L3-flush[{phase}]", {})
        flush = timing.get(f"host-flush[{phase}]", {})

        encode_ns = enc.get("total_ns", 0)
        roundtrip_ns = flush.get("total_ns", 0) + l3.get("total_ns", 0)
        host_wait_ns = sub.get("wait_ns", 0) + fence.get("total_ns", 0)
        guest_submit_ns = max(roundtrip_ns - sub.get("wait_ns", 0), 0)

        active = encode_ns + guest_submit_ns
        wait = host_wait_ns
        total = active + wait
        if total == 0:
            continue
        summary[phase] = {
            "encode_ns": encode_ns,
            "guest_submit_ns": guest_submit_ns,
            "host_wait_ns": host_wait_ns,
            "active_ns": active,
            "wait_ns": wait,
            "active_pct": round(100.0 * active / total, 2),
            "wait_pct": round(100.0 * wait / total, 2),
            "encode_pct": round(100.0 * encode_ns / total, 2),
            "roundtrips": sub.get("calls", 0),
            "host_flush_bytes": flush.get("bytes", 0),
        }
    return summary


# ---------------------------------------------------------------------------
# HTTP server appliance probe
# ---------------------------------------------------------------------------

def free_port() -> int:
    """Pick an unused localhost TCP port to use as a QEMU hostfwd target."""
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _query_llama_server(port: int, deadline: float, launch: float,
                        query: str, n_predict: int) -> dict:
    """Poll ``/health`` until ready, then send *query* and record the reply.

    The request goes to the OpenAI-compatible ``/v1/chat/completions`` endpoint
    (not the raw ``/completion``) so the server applies the model's chat
    template: an instruction-tuned model fed an un-templated prompt degenerates
    into special tokens, whereas the templated turn yields a real answer.
    Returns the round-trip timings together with the verbatim *query* and the
    model's generated reply, or ``{}`` if the server never became ready before
    *deadline*.
    """
    health = f"http://127.0.0.1:{port}/health"
    while time.monotonic() < deadline:
        try:
            started = time.monotonic()
            with urllib.request.urlopen(health, timeout=2) as response:
                body = response.read().decode("utf-8", "replace")
            metrics = {
                "http_status": response.status,
                "health": json.loads(body),
                "latency_s": round(time.monotonic() - started, 4),
                # First successful /health: the server is up and model-ready, so
                # this is the launch->ready boot time for the appliance.
                "boot_time_s": round(time.monotonic() - launch, 3),
            }
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/chat/completions",
                data=json.dumps({
                    "messages": [{"role": "user", "content": query}],
                    "max_tokens": n_predict,
                    "stream": False,
                }).encode(),
                headers={"Content-Type": "application/json"},
            )
            started = time.monotonic()
            with urllib.request.urlopen(request, timeout=deadline - time.monotonic()) as response:
                completion = json.loads(response.read().decode("utf-8", "replace"))
            elapsed = max(time.monotonic() - started, 0.0001)
            # OpenAI-compatible shape: choices[0].message.content + usage tokens.
            choice = (completion.get("choices") or [{}])[0]
            content = (choice.get("message") or {}).get("content", "")
            tokens = (completion.get("usage") or {}).get("completion_tokens", 0)
            metrics.update({
                "completion_status": response.status,
                # Record the actual exchange, so each server result carries a
                # real query->completion pair, not just aggregate timings.
                "query": query,
                "completion": content,
                "requests_per_s": round(1 / elapsed, 3),
                "tokens_per_s": round(tokens / elapsed, 3),
            })
            return metrics
        except Exception:
            time.sleep(0.5)
    return {}


def probe_http_server(
    command: list[str],
    *,
    port: int,
    ready_marker: str,
    query: str,
    timeout: float,
    n_predict: int = 64,
    cwd: str | Path | None = None,
) -> tuple[dict, str, bool]:
    """Boot an llama.cpp HTTP-server appliance, send one real query, score it.

    Launches *command* in the background (a QEMU invocation whose guest forwards
    its :8080 listener to host *port*), polls ``/health`` until the model is
    ready, then POSTs a single ``/v1/chat/completions`` carrying *query*. Returns
    ``(metrics, log, passed)``: *metrics* records the round-trip timings plus the
    verbatim *query* and the generated *completion* text; *log* is the merged
    serial output; *passed* is True when the guest printed *ready_marker* and
    both HTTP calls returned 200.
    """
    launch = time.monotonic()
    proc = subprocess.Popen(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        metrics = _query_llama_server(port, time.monotonic() + timeout, launch,
                                      query, n_predict)
        proc.terminate()
        log, _ = proc.communicate(timeout=10)
    except Exception:
        proc.kill()
        log, _ = proc.communicate()
        metrics = {}
    ready = ready_marker in log
    passed = (ready
              and metrics.get("http_status") == 200
              and metrics.get("completion_status") == 200)
    metrics["ready"] = ready
    # proc is reaped by communicate() above, so getrusage has its peak.
    metrics["peak_rss_kb"] = peak_child_rss_kb()
    return metrics, log, passed
