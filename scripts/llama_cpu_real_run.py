#!/usr/bin/env python3
"""Boot the upstream llama.cpp CPU appliance under QEMU and capture REAL
throughput (pp512/tg128) from the guest serial log.

This replaces the previous non-invasive stub for llm.bench.cpu: the appliance
actually runs on this host's CPU (KVM + -cpu host when /dev/kvm is usable, TCG
+ -cpu max otherwise) with the same -march=native ISA it was built for, so the
recorded numbers are genuine — never synthetic.

Model delivery matches apps/app-llama-upstream/common.h: a 9pfs share tagged
`model` whose `model.gguf` is mounted at /mnt/model. The GGUF is taken from
$VOGUE_CPU_MODEL (default: config/llama_env_matrix.json model.default_path).

Outcomes (honest, never faked):
  pass                       guest printed `uk-llama-upstream: pp512=.. tg128=..` + PASS
  blocked:qemu-missing       no qemu-system-x86_64
  blocked:unikraft-image-missing
  blocked:model-missing      no GGUF to stage
  blocked:no-pass-line       booted but no PASS evidence marker
"""
from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results" / "llama"
IMAGE = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-cpu_qemu-x86_64"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "upstream_cpu.json").write_text(json.dumps(payload, indent=2) + "\n")


def _blocker(status: str, next_step: str) -> int:
    _write({
        "schema": "llama/upstream-runtime.v2",
        "evidence_id": "llama-upstream-cpu",
        "status": status,
        "pass": False,
        "generated_utc": _now(),
        "claim_allowed": "Blocker documented; no throughput claim.",
        "claim_forbidden": "llama.cpp token/s without a same-run PASS marker.",
        "next_step": next_step,
    })
    print(f"llama-cpu-real-run: {status}")
    return 0


def _model_path() -> Path | None:
    env = os.environ.get("VOGUE_CPU_MODEL")
    if env:
        p = Path(env)
        return p if p.is_file() else None
    try:
        cfg = json.loads((ROOT / "config" / "llama_env_matrix.json").read_text())
        cand = ROOT / cfg["model"]["default_path"]
        if cand.is_file():
            return cand
    except (OSError, KeyError, json.JSONDecodeError):
        pass
    return None


def main() -> int:
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        return _blocker("blocked:qemu-missing", "Install qemu-system-x86 and rerun make llama-upstream-cpu-run.")
    if not IMAGE.exists():
        return _blocker("blocked:unikraft-image-missing", "run: make llama-upstream-cpu-build")
    model = _model_path()
    if model is None:
        return _blocker("blocked:model-missing",
                        "Set VOGUE_CPU_MODEL=/path/to/model.gguf (any GGUF) and rerun.")

    # KVM + -cpu host when the device is usable, else TCG + -cpu max.
    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")

    with tempfile.TemporaryDirectory(prefix="vogue-cpu-model-") as share:
        shutil.copy(model, Path(share) / "model.gguf")
        cmd = [
            qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", "4096",
            "-nographic", "-no-reboot", "-kernel", str(IMAGE),
            "-fsdev", f"local,id=myid,path={share},security_model=none",
            "-device", "virtio-9p-pci,fsdev=myid,mount_tag=model",
            "-append", "console=ttyS0",
        ]
        def _dec(x) -> str:
            if x is None:
                return ""
            return x.decode("utf-8", "replace") if isinstance(x, bytes) else x
        try:
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=600)
            out = proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as e:
            out = _dec(e.stdout) + _dec(e.stderr)

    (RESULTS / "upstream_cpu.log").write_text(out)
    (RESULTS / "upstream_cpu_serial.log").write_text(out)  # model_load_time_check reads *_serial.log
    m = re.search(r"uk-llama-upstream: pp512=([0-9.]+) tg128=([0-9.]+)", out)
    passed = "uk-llama-upstream: PASS evidence_id=llama-upstream-cpu" in out
    if not (m and passed):
        return _blocker("blocked:no-pass-line",
                        "Inspect results/llama/upstream_cpu.log; the guest did not emit a PASS marker.")

    pp512, tg128 = float(m.group(1)), float(m.group(2))
    _write({
        "schema": "llama/upstream-runtime.v2",
        "evidence_id": "llama-upstream-cpu",
        "status": "pass",
        "pass": True,
        "generated_utc": _now(),
        "pp512": pp512,
        "tg128": tg128,
        "accel": accel,
        "cpu": cpu,
        "host": platform.platform(),
        "model": model.name,
        "run_log": "results/llama/upstream_cpu.log",
        "claim_allowed": (f"Upstream llama.cpp CPU path boots on Unikraft (unmodified sources) and runs a real "
                          f"GGUF via 9pfs on this host ({accel}/-cpu {cpu}). pp512={pp512} tg128={tg128} t/s."),
        "claim_forbidden": "GPU throughput, Vulkan dispatch, or cross-host comparison without a matching baseline.",
    })
    print(f"llama-cpu-real-run: pass pp512={pp512} tg128={tg128} accel={accel} cpu={cpu}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
