#!/usr/bin/env python3
"""Host-side profiling wrappers around the Vulkan llama bench appliance.

Phase 1 of docs/plan-profile.md: run the *identical* QEMU bench invocation
that scripts/app-llama-vk.py uses, wrapped in one host-side profiling tool,
and capture a results/profile/<tool>.json artifact in the standard schema.

Tools:
  kvm-stat     perf kvm stat record       -> vmexit profile (exit reasons)
  host-record  perf record -g on QEMU     -> host CPU split (vCPU vs render)
  guest-record perf kvm --guest record    -> guest PCs, symbolized vs the
                                             unikernel ELF (flat, no ASLR)
  qemu-trace   QEMU --trace virtio_gpu_*  -> SUBMIT_3D / fence command rates

Every tool degrades to an honest blocked:* status instead of fabricating
numbers (no perf, no /dev/kvm, no image, no model, trace backend missing).
Raw profiler output is kept under results/profile/raw/ for flamegraphs.
"""
from __future__ import annotations

import argparse
import bisect
import importlib.util
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

from common import (
    acceleration,
    decode,
    normalize_arch,
    resolve_model,
    resolve_qemu,
    result,
    result_path,
    write_json,
)

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/profile"
RAW = RESULTS / "raw"

# Reuse the bench runner's QEMU command builder so the profiled run is the
# same workload as the evidence run (single source of truth).
_spec = importlib.util.spec_from_file_location(
    "app_llama_vk", Path(__file__).with_name("app-llama-vk.py"))
app_llama_vk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(app_llama_vk)

TOOLS = ("kvm-stat", "host-record", "guest-record", "qemu-trace")
BENCH_RE = re.compile(r"pp512=([0-9.]+)\s+tg128=([0-9.]+)")
# llama-bench fixed workload: 512 prompt + 128 generated tokens.
BENCH_TOKENS = {"prompt": 512, "generated": 128}


def perf_binary() -> str | None:
    return shutil.which("perf")


def check_blockers(args, image: Path, qemu: str | None, model) -> tuple[str, str] | None:
    if not qemu:
        return ("blocked:qemu-missing", "QEMU executable not found")
    if not model:
        return ("blocked:model-missing", "Model file not found")
    if not image.is_file():
        return ("blocked:image-missing",
                "Unikraft image not found (run make llama-vk-bench-build)")
    if args.tool != "qemu-trace":
        if sys.platform != "linux":
            return ("blocked:not-linux", "perf profiling requires a Linux host")
        if not perf_binary():
            return ("blocked:perf-missing", "perf not found on PATH")
        if acceleration(args.arch) != "kvm" and args.tool in ("kvm-stat", "guest-record"):
            return ("blocked:kvm-missing",
                    "/dev/kvm unavailable; vmexit/guest profiling needs KVM")
    return None


def bench_metrics(log: str) -> dict:
    match = BENCH_RE.search(log)
    metrics = {"bench_pass": bool(match and "PASS" in log)}
    if match:
        metrics["pp512"] = float(match.group(1))
        metrics["tg128"] = float(match.group(2))
    return metrics


def run_guest(command: list[str], timeout: int) -> tuple[str, int]:
    try:
        proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                              timeout=timeout, check=False)
        return proc.stdout + proc.stderr, proc.returncode
    except subprocess.TimeoutExpired as exc:
        return decode(exc.stdout) + decode(exc.stderr), -1


# ── kvm-stat ──────────────────────────────────────────────────────────────


def tool_kvm_stat(base: list[str], timeout: int) -> tuple[dict, str, list[str]]:
    data = RAW / "perf.kvm.data"
    command = [perf_binary(), "kvm", "stat", "record", "-o", str(data), "--", *base]
    log, _ = run_guest(command, timeout)
    metrics = bench_metrics(log)
    report = subprocess.run(
        [perf_binary(), "kvm", "stat", "report", "--event", "vmexit", "-i", str(data)],
        cwd=ROOT, text=True, capture_output=True, check=False)
    text = report.stdout + report.stderr
    (RAW / "kvm_stat_report.txt").write_text(text)
    exits = {}
    total = 0
    # Report rows: "  EXIT_REASON   count  pct%  ..." — keep name + count.
    for line in text.splitlines():
        row = re.match(r"\s*([A-Z][A-Z0-9_]+)\s+(\d+)\s", line)
        if row:
            exits[row.group(1)] = int(row.group(2))
            total += int(row.group(2))
    metrics["vmexits"] = exits
    metrics["vmexit_total"] = total
    if total and metrics.get("bench_pass"):
        metrics["vmexits_per_generated_token"] = round(
            total / BENCH_TOKENS["generated"], 1)
    return metrics, log, command


# ── host-record ───────────────────────────────────────────────────────────


def tool_host_record(base: list[str], timeout: int, freq: int) -> tuple[dict, str, list[str]]:
    data = RAW / "perf.host.data"
    command = [perf_binary(), "record", "-F", str(freq), "-g",
               "-o", str(data), "--", *base]
    log, _ = run_guest(command, timeout)
    metrics = bench_metrics(log)
    report = subprocess.run(
        [perf_binary(), "report", "--stdio", "--no-children",
         "-s", "comm,dso", "--percent-limit", "1", "-i", str(data)],
        cwd=ROOT, text=True, capture_output=True, check=False)
    text = report.stdout + report.stderr
    (RAW / "host_record_report.txt").write_text(text)
    rows = []
    for line in text.splitlines():
        row = re.match(r"\s*([0-9.]+)%\s+(\S+)\s+(\S+)", line)
        if row:
            rows.append({"pct": float(row.group(1)),
                         "comm": row.group(2), "dso": row.group(3)})
    metrics["top_threads"] = rows[:15]
    metrics["raw_perf_data"] = str(data.relative_to(ROOT))
    return metrics, log, command


# ── guest-record ──────────────────────────────────────────────────────────


def load_symbols(image: Path) -> tuple[list[int], list[str]]:
    """Sorted (address, name) table from the unikernel ELF (prefer .dbg)."""
    dbg = image.with_name(image.name + ".dbg")
    elf = dbg if dbg.is_file() else image
    out = subprocess.run(["nm", "-n", "-C", str(elf)], text=True,
                         capture_output=True, check=False).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1].lower() in ("t", "w"):
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def tool_guest_record(base: list[str], timeout: int, freq: int,
                      image: Path) -> tuple[dict, str, list[str]]:
    data = RAW / "perf.guest.data"
    command = [perf_binary(), "kvm", "--guest", "record", "-F", str(freq),
               "-o", str(data), "--", *base]
    log, _ = run_guest(command, timeout)
    metrics = bench_metrics(log)
    script = subprocess.run(
        [perf_binary(), "script", "-i", str(data), "-F", "ip"],
        cwd=ROOT, text=True, capture_output=True, check=False)
    ips = [int(tok, 16) for tok in script.stdout.split()
           if re.fullmatch(r"[0-9a-f]+", tok)]
    addrs, names = load_symbols(image)
    counts: Counter[str] = Counter()
    for ip in ips:
        idx = bisect.bisect_right(addrs, ip) - 1
        counts[names[idx] if idx >= 0 else "<unknown>"] += 1
    top = counts.most_common(40)
    (RAW / "guest_record_symbols.txt").write_text(
        "\n".join(f"{count:8d} {name}" for name, count in top) + "\n")
    metrics["samples"] = len(ips)
    metrics["symbols_resolved"] = bool(addrs)
    metrics["top_guest_functions"] = [
        {"fn": name, "samples": count} for name, count in top[:25]]
    metrics["raw_perf_data"] = str(data.relative_to(ROOT))
    return metrics, log, command


# ── qemu-trace ────────────────────────────────────────────────────────────


def tool_qemu_trace(base: list[str], timeout: int) -> tuple[dict, str, list[str]]:
    command = [*base, "--trace", "virtio_gpu_*"]
    log, _ = run_guest(command, timeout)
    metrics = bench_metrics(log)
    counts: Counter[str] = Counter()
    # Trace lines (log backend): "...virtio_gpu_<event> arg=..." one per line.
    for line in log.splitlines():
        event = re.search(r"\b(virtio_gpu_[a-z0-9_]+)\b", line)
        if event:
            counts[event.group(1)] += 1
    metrics["trace_events"] = dict(counts.most_common())
    metrics["trace_event_total"] = sum(counts.values())
    if counts and metrics.get("bench_pass"):
        metrics["events_per_generated_token"] = round(
            sum(counts.values()) / BENCH_TOKENS["generated"], 1)
    if not counts:
        metrics["note"] = ("no virtio_gpu_* trace lines seen; QEMU may lack "
                           "the log trace backend")
    return metrics, log, command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", choices=TOOLS, required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--freq", type=int, default=999,
                        help="perf sampling frequency (record tools)")
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or app_llama_vk.default_qemu_binary(arch)

    output = result_path(RESULTS, f"{args.tool.replace('-', '_')}.json", arch)
    image = app_llama_vk.image("bench", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    inputs = {"tool": args.tool, "arch": arch, "model": str(args.model),
              "image": str(image), "tokens": BENCH_TOKENS}

    blocker = check_blockers(args, image, qemu, model)
    if blocker:
        write_json(output, result(blocker[0], inputs=inputs, error=blocker[1]))
        print(f"profile-vk[{args.tool}]: {blocker[0]}")
        return 0

    RAW.mkdir(parents=True, exist_ok=True)
    import tempfile
    with tempfile.TemporaryDirectory(prefix="vogue-model-") as directory:
        shutil.copy(model, Path(directory) / "model.gguf")
        base = [
            *app_llama_vk.qemu_command(qemu, args.model, "bench",
                                       args.timeout, 0, arch),
            "-fsdev", f"local,id=model,path={directory},security_model=none",
            "-device", "virtio-9p-pci,fsdev=model,mount_tag=model",
        ]
        if args.tool == "kvm-stat":
            metrics, log, command = tool_kvm_stat(base, args.timeout)
        elif args.tool == "host-record":
            metrics, log, command = tool_host_record(base, args.timeout, args.freq)
        elif args.tool == "guest-record":
            metrics, log, command = tool_guest_record(base, args.timeout,
                                                      args.freq, image)
        else:
            metrics, log, command = tool_qemu_trace(base, args.timeout)

    if not metrics.get("bench_pass"):
        status = "blocked:no-pass-marker"
    elif args.tool == "kvm-stat" and not metrics.get("vmexit_total"):
        status = "blocked:perf-permission"
    elif args.tool == "guest-record" and not metrics.get("samples"):
        status = "blocked:perf-permission"
    else:
        status = "pass"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if status == "pass" else log[-2000:]))
    print(f"profile-vk[{args.tool}]: {status} -> {output.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
