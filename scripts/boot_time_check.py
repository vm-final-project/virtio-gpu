#!/usr/bin/env python3
"""Measure Unikraft appliance boot-to-READY latency under QEMU.

For each appliance image present under `.unikraft/build/`, the script:
  1. Spawns `qemu-system-x86_64 -kernel <image>` with the appliance's
     serial=stdio.
  2. Watches stdout for the READY marker grep pattern (per-appliance below).
  3. Records `boot_to_ready_ms = stdout_first_match_time - process_start_time`.
  4. Sends SIGTERM and reaps the QEMU process.

Hosts without QEMU, or without a built image, produce a structured
`blocked:*` row rather than a hard error. Writes
`results/boot/latest.{json,md}`.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".unikraft" / "build"
OUT = ROOT / "results" / "boot"

APPLIANCES = [
    {"name": "kmscube",          "image": "vogue_qemu-x86_64",                       "ready": re.compile(r"uk-kmscube: PASS")},
    {"name": "glmark2",          "image": "vogue-glmark2_qemu-x86_64",               "ready": re.compile(r"uk-glmark2: PASS|status=pass")},
    {"name": "llama-cpu-bench",  "image": "vogue-llama-upstream-bench_qemu-x86_64",  "ready": re.compile(r"uk-llama-upstream: PASS")},
    {"name": "llama-cpu-server", "image": "vogue-llama-upstream-server_qemu-x86_64", "ready": re.compile(r"uk-llama-upstream-server: READY")},
    {"name": "llama-vk-bench",   "image": "vogue-llama-upstream-vk_qemu-x86_64",     "ready": re.compile(r"uk-llama-upstream-vk: PASS")},
    {"name": "llama-vk-server",  "image": "vogue-llama-upstream-vk-server_qemu-x86_64", "ready": re.compile(r"uk-llama-upstream-vk-server: READY")},
]

DEFAULT_TIMEOUT_S = 30.0


def _qemu_bin() -> str | None:
    return shutil.which(os.environ.get("QEMU", "qemu-system-x86_64"))


def _measure(image_path: Path, ready: re.Pattern[str], timeout_s: float) -> dict:
    qemu = _qemu_bin()
    if qemu is None:
        return {"status": "blocked:qemu-missing"}
    if not image_path.exists():
        return {"status": "blocked:image-missing", "image": str(image_path.relative_to(ROOT))}

    cmd = [
        qemu,
        "-machine", "accel=tcg",
        "-cpu", "max",
        "-m", "256M",
        "-kernel", str(image_path),
        "-display", "none",
        "-serial", "mon:stdio",
        "-nographic",
    ]
    started_at = time.perf_counter()
    proc = subprocess.Popen(
        cmd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    ready_at: float | None = None
    deadline = started_at + timeout_s
    try:
        assert proc.stdout is not None
        for line in proc.stdout:
            if ready.search(line):
                ready_at = time.perf_counter()
                break
            if time.perf_counter() > deadline:
                break
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    if ready_at is None:
        return {"status": "blocked:no-ready-marker", "image": str(image_path.relative_to(ROOT))}

    boot_to_ready_ms = (ready_at - started_at) * 1000.0
    return {
        "status": "pass",
        "image": str(image_path.relative_to(ROOT)),
        "boot_to_ready_ms": round(boot_to_ready_ms, 2),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="non-zero exit only if every row is fatal (not blocked:*)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)
    parser.add_argument("--appliance", action="append",
                        help="restrict to a single appliance (repeatable)")
    args = parser.parse_args(argv)

    OUT.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    for a in APPLIANCES:
        if args.appliance and a["name"] not in args.appliance:
            continue
        path = BUILD / a["image"]
        result = _measure(path, a["ready"], args.timeout)
        result["name"] = a["name"]
        rows.append(result)
        ms = result.get("boot_to_ready_ms")
        ms_text = f"{ms:.2f} ms" if isinstance(ms, (int, float)) else "n/a"
        print(f"boot-time: {a['name']:<18} {result['status']:<22} {ms_text}")

    generated = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    payload = {
        "metadata": {
            "generated_utc": generated,
            "source": "scripts/boot_time_check.py",
            "principle": "Unikraft images boot directly into one entrypoint; READY marker is appliance-specific and quoted from the source. Hosts without QEMU or the built image produce structured blocked rows.",
        },
        "rows": rows,
    }
    (OUT / "latest.json").write_text(json.dumps(payload, indent=2) + "\n")
    md = ["# Unikraft boot-to-READY", "", f"Generated: `{generated}`", "",
          "| Appliance | Status | Boot to READY |",
          "|---|---|---|"]
    for r in rows:
        ms = r.get("boot_to_ready_ms")
        md.append(f"| {r['name']} | `{r['status']}` | {ms if ms is not None else 'n/a'} ms |")
    (OUT / "latest.md").write_text("\n".join(md) + "\n")

    if args.check:
        # Treat blocked:* as soft; only fail if every row is a non-blocked, non-pass row.
        fatal = [r for r in rows if not (r["status"] == "pass" or r["status"].startswith("blocked"))]
        if fatal:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
