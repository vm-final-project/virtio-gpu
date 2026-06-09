#!/usr/bin/env python3
"""Measured throughput gate for the llama.cpp Vulkan HTTP server appliance."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from artifact_utils import blocked_artifact, load_required_json, make_artifact, write_json

ROOT = Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "scripts" / "llama_server_vk_capture.py"
SERVER_JSON = ROOT / "results" / "llama" / "llama_server_vk.json"
OUT = ROOT / "results" / "llama"
SOURCE = "scripts/llm_server_vk_throughput_check.py"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="non-zero exit only on a hard failure (a blocker passes)")
    ap.add_argument("--requests", type=int,
                    default=int(os.environ.get("VOGUE_SRV_THROUGHPUT_REQS", "8")))
    args = ap.parse_args(argv)

    env = {**os.environ, "VOGUE_SRV_THROUGHPUT_REQS": str(args.requests)}
    subprocess.run([sys.executable, str(CAPTURE)], cwd=ROOT, env=env, check=False)

    server = load_required_json(
        SERVER_JSON,
        source=SOURCE,
        headline="Measured throughput for the Vulkan llama.cpp HTTP server",
        missing_status="blocked:no-server-json",
        bad_json_status="blocked:bad-runtime-json",
        next_step="Rerun llama_server_vk_capture.py to regenerate results/llama/llama_server_vk.json.",
    )
    if server.get("metadata"):
        payload = server
        rc = 0
    else:
        status = str(server.get("status", "blocked:no-server-json"))
        tput = (server.get("http") or {}).get("throughput") or {}
        if status == "pass" and tput.get("requests_per_s"):
            payload = make_artifact(
                source=SOURCE,
                status="pass",
                headline="Measured throughput for the Vulkan llama.cpp HTTP server",
                counts={
                    "requests": tput.get("requests"),
                    "ok": tput.get("ok"),
                    "failed": tput.get("failed"),
                },
                artifacts={"server_runtime_json": "results/llama/llama_server_vk.json"},
                checks=[
                    {"id": "server_runtime", "status": status},
                    {"id": "http_throughput", "status": "pass"},
                ],
                extra={
                    "transport": server.get("transport"),
                    "venus_device": server.get("venus_device"),
                    "requests": tput.get("requests"),
                    "concurrency": tput.get("concurrency"),
                    "n_predict": tput.get("n_predict"),
                    "ok": tput.get("ok"),
                    "failed": tput.get("failed"),
                    "wall_s": tput.get("wall_s"),
                    "requests_per_s": tput.get("requests_per_s"),
                    "tokens_per_s": tput.get("tokens_per_s"),
                    "decode_tps_mean": tput.get("decode_tps_mean"),
                    "decode_tps_max": tput.get("decode_tps_max"),
                    "prompt_tps_mean": tput.get("prompt_tps_mean"),
                    "mean_latency_s": tput.get("mean_latency_s"),
                    "ttft_s": tput.get("ttft_s"),
                    "claim_allowed": (
                        "Measured same-run HTTP throughput for the Vulkan llama.cpp server over real Venus on the recorded model."
                    ),
                    "claim_forbidden": (
                        "Cross-host / cross-model comparison or peak-capacity claim beyond this bounded same-run burst."
                    ),
                },
            )
            rc = 0
        else:
            blocked = status if status.startswith("blocked:") else "blocked:no-throughput"
            payload = blocked_artifact(
                source=SOURCE,
                status=blocked,
                headline="Measured throughput for the Vulkan llama.cpp HTTP server",
                stage="runtime",
                claim_allowed="Throughput blocker recorded; no requests/s or tokens/s claim.",
                claim_forbidden="Passing HTTP throughput claim while the server runtime is not in a serving state.",
                next_step="Fix the Vulkan server runtime blocker in results/llama/llama_server_vk.json, then rerun llm_server_vk_throughput_check.py.",
                counts={"requests": args.requests},
                artifacts={"server_runtime_json": "results/llama/llama_server_vk.json"},
                checks=[
                    {"id": "server_runtime", "status": status},
                    {"id": "http_throughput", "status": blocked},
                ],
            )
            rc = 0 if blocked.startswith("blocked:") else 1

    write_json(OUT / "server_vk_throughput.json", payload)
    print(
        f"llm-server-vk-throughput: {payload['status']}"
        + (
            f" requests/s={payload['requests_per_s']} tokens/s={payload['tokens_per_s']} ttft={payload['ttft_s']}s"
            if payload["status"] == "pass"
            else ""
        )
    )
    return rc if args.check else 0


if __name__ == "__main__":
    raise SystemExit(main())
