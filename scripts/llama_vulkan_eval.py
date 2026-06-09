#!/usr/bin/env python3
"""Concise llama.cpp / ggml-vulkan evidence collector.

Supported gates intentionally mirror the minimized codebase:
- n3-dispatch: host-native regression for libvulkan static Vulkan/Venus dispatch.
- llama-cpu: structured runtime artifact for the true upstream llama.cpp CPU appliance.
- llama-vk: structured runtime artifact for the true upstream llama.cpp Vulkan/Venus appliance.
- linux-baseline/probe/build/run/bench: reference-baseline placeholders that preserve claim
  boundaries when same-run evidence is not present.

No synthetic local llama substrate evidence is emitted here.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from artifact_utils import blocked_artifact, make_artifact, write_json

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results" / "llama"


def blocker(name: str, reason: str, *, next_step: str = "") -> int:
    payload = blocked_artifact(
        source="scripts/llama_vulkan_eval.py",
        status=reason,
        headline=f"llama.cpp Vulkan evidence gate: {name}",
        stage="runtime",
        claim_allowed="Structured blocker recorded; no llama.cpp throughput claim.",
        claim_forbidden="Token/s, GPU acceleration, or successful Unikraft runtime claim.",
        next_step=next_step,
        extra={"evidence_id": name.replace("_", "-")},
    )
    write_json(RESULTS / f"{name}.json", payload)
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
            m3 = re.search(r"vulkan_dispatch_core_test:\s+PASS checks=(\d+)", out)
            if m3:
                passed = int(m3.group(1))
                failed = 0
                total = passed
            else:
                passed = 0
                failed = 0 if proc.returncode == 0 and "PASS" in out else 1
                total = passed + failed
    status = "pass" if proc.returncode == 0 and failed == 0 else "blocked:dispatch-test-failed"
    write_json(RESULTS / "vulkan_n3_dispatch.json", make_artifact(
        source="scripts/llama_vulkan_eval.py",
        status=status,
        headline="llama.cpp Vulkan static dispatch regression",
        counts={"checks_passed": passed, "checks_failed": failed, "checks_total": total},
        checks=[{"id": "vulkan_dispatch_core_test", "status": status}],
        extra={
            "evidence_id": "llama-vk-n3-dispatch",
            "pass": status == "pass",
            "checks_passed": passed,
            "checks_failed": failed,
            "checks_total": total,
            "stdout_tail": out[-4000:],
            "claim_allowed": "Pinned upstream ggml-vulkan Vulkan C ABI dispatch works through libvulkan native regression tests.",
            "claim_forbidden": "QEMU/Venus runtime success, token generation, or throughput.",
        },
    ))
    print(f"vk.ggml-dispatch {status} passed={passed} failed={failed} total={total}")
    return 0 if status == "pass" else 1


def upstream_runtime(kind: str) -> int:
    assert kind in {"cpu", "vk"}
    if kind == "cpu":
        name = "llama_cpu"
        image = ROOT / ".unikraft" / "build" / "vogue-llama-cpu_qemu-x86_64"
        evidence = "llama-cpu"
        allowed = "Upstream llama.cpp CPU appliance runtime when same-run PASS marker exists."
        next_step = "run: make llama-cpu-build && make llama-cpu-run"
    else:
        name = "llama_vk"
        image = ROOT / ".unikraft" / "build" / "vogue-llama-vk_qemu-x86_64"
        evidence = "llama-vk"
        allowed = "Upstream llama.cpp Vulkan/Venus appliance runtime when same-run PASS marker exists."
        next_step = "run: make llama-vk-build && make llama-vk-run"
    if not image.exists():
        write_json(RESULTS / f"{name}.json", blocked_artifact(
            source="scripts/llama_vulkan_eval.py",
            status="blocked:unikraft-image-missing",
            headline=f"llama.cpp upstream runtime gate: {name}",
            stage="artifact-missing",
            first_missing_dependency=str(image.relative_to(ROOT)),
            claim_allowed="Image blocker documented; no runtime or throughput claim.",
            claim_forbidden="llama.cpp token/s or acceleration without a booted single-app image.",
            next_step=next_step,
            extra={"evidence_id": evidence, "pass": False, "image": str(image.relative_to(ROOT))},
        ))
        if kind == "vk":
            write_json(RESULTS / "env10_real.json", blocked_artifact(
                source="scripts/llama_vulkan_eval.py",
                status="blocked:unikraft-image-missing",
                headline="llama.cpp ENV10 real Venus runtime gate",
                stage="artifact-missing",
                first_missing_dependency=str(image.relative_to(ROOT)),
                claim_allowed="ENV10 blocker documented; no real Venus throughput claim.",
                claim_forbidden="Unikraft Vulkan throughput without same-run PASS evidence.",
                next_step=next_step,
                extra={"evidence_id": "env10-real", "pass": False},
            ))
        print(f"LLAMA-{kind.upper()} blocked: image missing")
        return 0
    # Keep runtime non-invasive in CI: record that a bootable image exists and
    # the gate still needs a same-run serial PASS capture to promote throughput.
    write_json(RESULTS / f"{name}.json", blocked_artifact(
        source="scripts/llama_vulkan_eval.py",
        status="blocked:runtime-capture-required",
        headline=f"llama.cpp runtime gate: {name}",
        stage="runtime",
        claim_allowed=allowed,
        claim_forbidden="Throughput claim until serial log contains the PASS evidence marker.",
        next_step=next_step,
        extra={"evidence_id": evidence, "pass": False, "image": str(image.relative_to(ROOT))},
    ))
    print(f"LLAMA-{kind.upper()} blocked: runtime capture required")
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
    parser.add_argument("mode", choices=["n3-dispatch", "llama-cpu", "llama-vk", "linux-baseline", "probe", "build", "run", "bench"])
    args = parser.parse_args()
    if args.mode == "n3-dispatch":
        return n3_dispatch()
    if args.mode == "llama-cpu":
        return upstream_runtime("cpu")
    if args.mode == "llama-vk":
        # Real boot through QEMU virtio-gpu-gl venus=true (no longer a stub).
        # llama_vk_real_run.py writes the honest same-run artifacts; if the image
        # is missing it falls back to the documented image-missing blocker.
        if (ROOT / ".unikraft" / "build" / "vogue-llama-vk_qemu-x86_64").exists():
            import subprocess as _sp
            return _sp.call([sys.executable, str(ROOT / "scripts" / "llama_vk_real_run.py")])
        return upstream_runtime("vk")
    return reference_gate(args.mode)


if __name__ == "__main__":
    raise SystemExit(main())
