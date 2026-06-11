#!/usr/bin/env python3
"""Run the host Vulkan smoke benchmark and write its canonical result."""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess

from common import decode, result, write_json

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "vulkan" / "vulkan_perf.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=int, required=True)
    args = parser.parse_args()

    binary = ROOT / "tests" / "build" / "vulkan_compute_test"
    if not binary.is_file():
        message = f"missing test binary: {binary}"
        write_json(OUT, result("blocked:binary-missing", error=message))
        return 0

    command = [str(binary)]
    try:
        run = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        write_json(OUT, result("blocked:timeout", command, error="Vulkan test timed out"))
        return 0

    text = decode(run.stdout)
    metrics = {"output_tail": text[-2000:]}
    for key in ("alloc_avg_us", "map_avg_us", "fence_avg_us", "device_extensions"):
        match = re.search(rf"{key}=(\d+)", text)
        if match:
            metrics[key] = int(match.group(1))
    passed = run.returncode == 0 and "PASS" in text
    write_json(OUT, result("pass" if passed else "fail", command, metrics=metrics,
                           error=None if passed else "Vulkan test failed"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
