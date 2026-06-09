#!/usr/bin/env python3
"""Performance/evaluation artifact generator for the Venus gate."""
from __future__ import annotations

import argparse
import statistics
import subprocess
import time
from pathlib import Path

from artifact_utils import blocked_artifact, load_json, make_artifact, write_json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "venus"


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    idx = int(p * (len(values) - 1))
    return sorted(values)[idx]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repetitions", type=int, default=30)
    ap.add_argument("--allow-blocked", action="store_true")
    args = ap.parse_args()

    latencies: list[float] = []
    statuses: list[int] = []
    for _ in range(max(1, args.repetitions)):
        t0 = time.perf_counter_ns()
        proc = subprocess.run(
            ["python3", "scripts/real_driver_static_check.py"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        latencies.append((time.perf_counter_ns() - t0) / 1e6)
        statuses.append(proc.returncode)

    qemu_path = OUT / "qemu_2d_probe.json"
    qemu = load_json(qemu_path)
    qemu_status = qemu.get("status", "missing")
    status = "pass" if all(s == 0 for s in statuses) else "fail"
    acceleration_status = (
        "ready-for-venus-smoke" if qemu_status == "pass" else "blocked:host-visible-or-qemu-gate"
    )
    summary_counts = {
        "repetitions": len(latencies),
        "static_gate_failures": sum(1 for s in statuses if s != 0),
    }
    checks = [
        {"id": "real_driver_static_check", "status": status},
        {"id": "qemu_2d_probe", "status": qemu_status},
    ]
    extra = {
        "acceleration_status": acceleration_status,
        "static_gate_latency_ms": {
            "mean": statistics.mean(latencies),
            "median": statistics.median(latencies),
            "min": min(latencies),
            "max": max(latencies),
            "p95": percentile(latencies, 0.95),
        },
        "qemu_probe_status": qemu_status,
        "claim_boundary": (
            "Static/native gates are measured here; STK/Vulkan acceleration is blocked until QEMU Venus probe is pass."
        ),
    }

    if qemu_status.startswith("blocked:"):
        payload = blocked_artifact(
            source="scripts/venus_perf_eval.py",
            status=qemu_status,
            headline="Venus static gate and QEMU readiness correlation",
            stage="runtime",
            first_missing_dependency=qemu.get("block", {}).get("first_missing_dependency"),
            claim_allowed="Static gate results are preserved; runtime acceleration remains explicitly blocked.",
            claim_forbidden="Real Venus acceleration or runtime pass claim without a passing QEMU Venus probe.",
            next_step=qemu.get("block", {}).get("next_step", "Resolve the QEMU Venus blocker and rerun venus_perf_eval.py."),
            counts=summary_counts,
            artifacts={"qemu_probe_json": "results/venus/qemu_2d_probe.json"},
            checks=checks,
            extra=extra,
        )
    else:
        payload = make_artifact(
            source="scripts/venus_perf_eval.py",
            status=status,
            headline="Venus static gate and QEMU readiness correlation",
            counts=summary_counts,
            artifacts={"qemu_probe_json": "results/venus/qemu_2d_probe.json"},
            checks=checks,
            extra=extra,
        )

    write_json(OUT / "venus_perf.json", payload)
    print(f"venus_perf_eval: {payload['status']} acceleration={acceleration_status} qemu={qemu_status}")
    return 0 if status == "pass" and (args.allow_blocked or acceleration_status == "ready-for-venus-smoke") else 1


if __name__ == "__main__":
    raise SystemExit(main())
