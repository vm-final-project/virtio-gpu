#!/usr/bin/env python3
"""Concise llama.cpp / ggml-vulkan evidence collector.

Supported gates intentionally mirror the minimized codebase:
- n3-dispatch: host-native regression for libvulkan static Vulkan/Venus dispatch.
- upstream-cpu: structured runtime artifact for the true upstream llama.cpp CPU appliance.
- upstream-vk: structured runtime artifact for the true upstream llama.cpp Vulkan/Venus appliance.
- linux-baseline/probe/build/run/bench: reference-baseline placeholders that preserve claim
  boundaries when same-run evidence is not present.

No synthetic local llama substrate evidence is emitted here.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results" / "llama"


def write(name: str, payload: dict) -> dict:
    RESULTS.mkdir(parents=True, exist_ok=True)
    payload.setdefault("generated_utc", time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    (RESULTS / f"{name}.json").write_text(json.dumps(payload, indent=2) + "\n")
    return payload


def blocker(name: str, reason: str, *, next_step: str = "") -> int:
    payload = {
        "schema": "llama/vulkan-eval.v2",
        "evidence_id": name.replace("_", "-"),
        "status": reason,
        "claim_allowed": "Structured blocker recorded; no llama.cpp throughput claim.",
        "claim_forbidden": "Token/s, GPU acceleration, or successful Unikraft runtime claim.",
    }
    if next_step:
        payload["next_step"] = next_step
    write(name, payload)
    print(f"{name}: {reason}")
    return 0


def n3_dispatch() -> int:
    proc = subprocess.run(["make", "-C", str(ROOT / "tests"), "test-dispatch"],
                          cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=120, check=False)
    out = proc.stdout
    m = re.search(r"passed=(\d+) failed=(\d+) total=(\d+)", out)
    if m:
        passed, failed, total = map(int, m.groups())
    else:
        m2 = re.search(r"Results:\s+(\d+) passed,\s+(\d+) failed", out)
        if m2:
            passed, failed = map(int, m2.groups())
            total = passed + failed
        else:
            m3 = re.search(r"vulkan_dispatch_test:\s+PASS checks=(\d+)", out)
            if m3:
                passed = int(m3.group(1))
                failed = 0
                total = passed
            else:
                passed = 0
                failed = 0 if proc.returncode == 0 and "PASS" in out else 1
                total = passed + failed
    status = "pass" if proc.returncode == 0 and failed == 0 else "blocked:dispatch-test-failed"
    write("vulkan_n3_dispatch", {
        "schema": "llama/vulkan-dispatch.v2",
        "evidence_id": "llama-vk-n3-dispatch",
        "status": status,
        "pass": status == "pass",
        "checks_passed": passed,
        "checks_failed": failed,
        "checks_total": total,
        "stdout_tail": out[-4000:],
        "claim_allowed": "Pinned upstream ggml-vulkan Vulkan C ABI dispatch works through libvulkan native regression tests.",
        "claim_forbidden": "QEMU/Venus runtime success, token generation, or throughput.",
    })
    print(f"vk.ggml-dispatch {status} passed={passed} failed={failed} total={total}")
    return 0 if status == "pass" else 1


def upstream_runtime(kind: str) -> int:
    assert kind in {"cpu", "vk"}
    if kind == "cpu":
        name = "upstream_cpu"
        image = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-cpu_qemu-x86_64"
        evidence = "llama-upstream-cpu"
        allowed = "Upstream llama.cpp CPU appliance runtime when same-run PASS marker exists."
        next_step = "run: make llama-upstream-cpu-build && make llama-upstream-cpu-run"
    else:
        name = "upstream_vk"
        image = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-vk_qemu-x86_64"
        evidence = "llama-upstream-vk"
        allowed = "Upstream llama.cpp Vulkan/Venus appliance runtime when same-run PASS marker exists."
        next_step = "run: make llama-upstream-vk-build && make llama-upstream-vk-run"
    if not image.exists():
        write(name, {
            "schema": "llama/upstream-runtime.v2",
            "evidence_id": evidence,
            "status": "blocked:unikraft-image-missing",
            "pass": False,
            "image": str(image.relative_to(ROOT)),
            "claim_allowed": "Image blocker documented; no runtime or throughput claim.",
            "claim_forbidden": "llama.cpp token/s or acceleration without a booted single-app image.",
            "next_step": next_step,
        })
        if kind == "vk":
            write("env10_real", {
                "schema": "llama/env10-real.v2",
                "evidence_id": "env10-real",
                "status": "blocked:unikraft-image-missing",
                "pass": False,
                "claim_allowed": "ENV10 blocker documented; no real Venus throughput claim.",
                "claim_forbidden": "Unikraft Vulkan throughput without same-run PASS evidence.",
                "next_step": next_step,
            })
        print(f"LLAMA-UPSTREAM-{kind.upper()} blocked: image missing")
        return 0
    # Keep runtime non-invasive in CI: record that a bootable image exists and
    # the gate still needs a same-run serial PASS capture to promote throughput.
    write(name, {
        "schema": "llama/upstream-runtime.v2",
        "evidence_id": evidence,
        "status": "blocked:runtime-capture-required",
        "pass": False,
        "image": str(image.relative_to(ROOT)),
        "claim_allowed": allowed,
        "claim_forbidden": "Throughput claim until serial log contains the PASS evidence marker.",
        "next_step": next_step,
    })
    print(f"LLAMA-UPSTREAM-{kind.upper()} blocked: runtime capture required")
    return 0


def reference_gate(mode: str) -> int:
    mapping = {
        "linux-baseline": "vulkan_linux_baseline",
        "probe": "vulkan_probe",
        "build": "vulkan_build",
        "run": "vulkan_run",
        "bench": "vulkan_bench",
    }
    reasons = {
        "linux-baseline": "blocked:linux-baseline-not-captured",
        "probe": "blocked:runtime-probe-not-captured",
        "build": "blocked:upstream-vk-build-not-captured",
        "run": "blocked:upstream-vk-run-not-captured",
        "bench": "blocked:throughput-not-captured",
    }
    return blocker(mapping[mode], reasons[mode], next_step="Use config/llama_env_matrix.json and same-run logs to promote this row.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["n3-dispatch", "upstream-cpu", "upstream-vk", "linux-baseline", "probe", "build", "run", "bench"])
    args = parser.parse_args()
    if args.mode == "n3-dispatch":
        return n3_dispatch()
    if args.mode == "upstream-cpu":
        return upstream_runtime("cpu")
    if args.mode == "upstream-vk":
        # Real boot through QEMU virtio-gpu-gl venus=true (no longer a stub).
        # llama_vk_real_run.py writes the honest same-run artifacts; if the image
        # is missing it falls back to the documented image-missing blocker.
        if (ROOT / ".unikraft" / "build" / "vogue-llama-upstream-vk_qemu-x86_64").exists():
            import subprocess as _sp
            return _sp.call([sys.executable, str(ROOT / "scripts" / "llama_vk_real_run.py")])
        return upstream_runtime("vk")
    return reference_gate(args.mode)


if __name__ == "__main__":
    raise SystemExit(main())
