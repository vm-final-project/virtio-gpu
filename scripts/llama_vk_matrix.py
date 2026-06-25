#!/usr/bin/env python3
"""Run a small model/memory matrix through the Vulkan llama runner."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/llama/llama_vk_matrix.json"


def plan_rows(small_model: str, large_model: str,
              qemu_mem_mb: tuple[int, ...],
              gpu_hostmem: tuple[str, ...]) -> list[dict]:
    rows = []
    for label, model in (("small", small_model), ("large", large_model)):
        for mem in qemu_mem_mb:
            for hostmem in gpu_hostmem:
                rows.append({
                    "model_label": label,
                    "model": model,
                    "qemu_mem_mb": mem,
                    "gpu_hostmem": hostmem,
                })
    return rows


def run_row(row: dict, args: argparse.Namespace) -> dict:
    env = os.environ.copy()
    env["VOGUE_QEMU_MEM_MB"] = str(row["qemu_mem_mb"])
    env["VOGUE_GPU_HOSTMEM"] = row["gpu_hostmem"]
    if args.renderer_expect:
        env["VOGUE_RENDERER_EXPECT"] = args.renderer_expect
    if args.egl_rendernode:
        env["VOGUE_EGL_RENDERNODE"] = args.egl_rendernode

    cmd = [
        sys.executable,
        str(ROOT / "scripts/app-llama-vk.py"),
        "--arch", args.arch,
        "--mode", "bench",
        "--bench-kind", args.bench_kind,
        "--model", row["model"],
        "--qemu", args.qemu,
        "--timeout", str(args.timeout),
        "--smp", str(args.smp),
    ]
    proc = subprocess.run(cmd, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          check=False)
    row_out = dict(row)
    row_out["command"] = cmd
    row_out["runner_rc"] = proc.returncode
    row_out["runner_output_tail"] = proc.stdout[-2000:]
    statuses = re.findall(r"llama-vk-[a-z_]+:\s*(pass|blocked:[^\s]+)", proc.stdout)
    row_out["status"] = statuses[-1] if statuses else "blocked:no-status"
    return row_out


def summarize_rows(rows: list[dict]) -> dict:
    passed = sum(1 for row in rows if row.get("status") == "pass")
    blocked = sum(1 for row in rows if str(row.get("status", "")).startswith("blocked:"))
    invalid = len(rows) - passed - blocked
    if rows and blocked == 0 and invalid == 0:
        status = "pass"
    elif blocked:
        status = "blocked:matrix-has-blocked"
    else:
        status = "blocked:matrix-incomplete"
    return {
        "status": status,
        "metrics": {
            "rows": len(rows),
            "pass": passed,
            "blocked": blocked,
            "invalid": invalid,
        },
        "rows": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--small-model", required=True)
    parser.add_argument("--large-model", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--smp", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--bench-kind", choices=("hand_rolled_smoke", "upstream_llama_bench"),
                        default="upstream_llama_bench")
    parser.add_argument("--renderer-expect", choices=("any", "software", "hardware"),
                        default="any")
    parser.add_argument("--egl-rendernode")
    args = parser.parse_args()

    rows = plan_rows(
        args.small_model,
        args.large_model,
        qemu_mem_mb=(4096, 8192),
        gpu_hostmem=("4G", "8G"),
    )
    output = summarize_rows([run_row(row, args) for row in rows])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps({"rows": output["metrics"]["rows"], "path": str(OUT)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
