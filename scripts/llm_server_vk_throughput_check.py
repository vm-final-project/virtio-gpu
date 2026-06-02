#!/usr/bin/env python3
"""Measured throughput gate for the llama.cpp Vulkan HTTP server appliance.

Boots the `llm.server.vk` appliance over real Venus (via
llama_server_vk_capture.py), then drives a bounded burst of HTTP /completion
requests against the in-guest upstream server and records *measured*
requests/s, tokens/s, and time-to-first-token. Numbers come from same-run host
probes — there is no synthetic estimate.

Writes results/llama/server_vk_throughput.{json,md}. Hosts without QEMU/GPU/model
fall back to the capture's structured blocker, which `--check` accepts (parity
with the other QEMU-dependent gates).
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "scripts" / "llama_server_vk_capture.py"
SERVER_JSON = ROOT / "results" / "llama" / "upstream_server_vk.json"
OUT = ROOT / "results" / "llama"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="non-zero exit only on a hard failure (a blocker passes)")
    ap.add_argument("--requests", type=int,
                    default=int(os.environ.get("VOGUE_SRV_THROUGHPUT_REQS", "8")))
    args = ap.parse_args(argv)

    env = {**os.environ, "VOGUE_SRV_THROUGHPUT_REQS": str(args.requests)}
    # Reuse the serving capture as the boot+probe harness; it records the burst
    # under http.throughput in results/llama/upstream_server_vk.json.
    subprocess.run([sys.executable, str(CAPTURE)], cwd=ROOT, env=env, check=False)

    server = json.loads(SERVER_JSON.read_text()) if SERVER_JSON.exists() else {}
    status = server.get("status", "blocked:no-server-json")
    tput = (server.get("http") or {}).get("throughput") or {}

    if status == "pass" and tput.get("requests_per_s"):
        payload = {
            "schema": "llama/server-vk-throughput.v1",
            "generated_utc": _now(),
            "status": "pass",
            "source": "scripts/llm_server_vk_throughput_check.py",
            "transport": server.get("transport"),
            "venus_device": server.get("venus_device"),
            "requests": tput.get("requests"),
            "n_predict": tput.get("n_predict"),
            "ok": tput.get("ok"),
            "failed": tput.get("failed"),
            "wall_s": tput.get("wall_s"),
            "requests_per_s": tput.get("requests_per_s"),
            "tokens_per_s": tput.get("tokens_per_s"),
            "mean_latency_s": tput.get("mean_latency_s"),
            "ttft_s": tput.get("ttft_s"),
            "claim_allowed": ("Measured same-run HTTP throughput for the Vulkan llama.cpp "
                              "server over real Venus on the recorded model."),
            "claim_forbidden": ("Cross-host / cross-model comparison or peak-capacity claim "
                                "beyond this bounded same-run burst."),
        }
        rc = 0
    else:
        payload = {
            "schema": "llama/server-vk-throughput.v1",
            "generated_utc": _now(),
            "status": status if status.startswith("blocked:") else "blocked:no-throughput",
            "source": "scripts/llm_server_vk_throughput_check.py",
            "detail": "Server did not reach a serving state; see upstream_server_vk.json.",
        }
        # A documented blocker is acceptable for --check (host has no GPU/QEMU/model);
        # a hard non-blocker failure is not.
        rc = 0 if payload["status"].startswith("blocked:") else 1

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "server_vk_throughput.json").write_text(json.dumps(payload, indent=2) + "\n")
    md = ["# llm.server.vk throughput", "", f"Generated: `{payload['generated_utc']}`",
          "", f"status = `{payload['status']}`"]
    if payload["status"] == "pass":
        md += ["",
               f"- requests: {payload['ok']}/{payload['requests']} ok in {payload['wall_s']}s",
               f"- requests/s: **{payload['requests_per_s']}**",
               f"- tokens/s: **{payload['tokens_per_s']}**",
               f"- mean latency: {payload['mean_latency_s']}s",
               f"- time-to-first-token: {payload['ttft_s']}s",
               f"- device: {payload.get('venus_device')}"]
    (OUT / "server_vk_throughput.md").write_text("\n".join(md) + "\n")

    print(f"llm-server-vk-throughput: {payload['status']}"
          + (f" requests/s={payload['requests_per_s']} tokens/s={payload['tokens_per_s']}"
             f" ttft={payload['ttft_s']}s" if payload['status'] == "pass" else ""))
    return rc if args.check else 0


if __name__ == "__main__":
    raise SystemExit(main())
