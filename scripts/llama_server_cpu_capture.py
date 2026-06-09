#!/usr/bin/env python3
"""Boot the upstream llama.cpp CPU *server* appliance under QEMU and capture the
READY-line evidence for llm.server.cpu.

The server entrypoint loads the model, initialises a context per slot, prints
`uk-llama-upstream-server: READY ...` and stays alive (the HTTP listener is
gated on the Unikraft netdev/lwIP path). We boot it with a bounded timeout on
this host's CPU (KVM + -cpu host when available), confirm the READY marker
appears, and record honest evidence — status=pass only when READY is observed.

Model: $VOGUE_CPU_MODEL or config/llama_env_matrix.json model.default_path.
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
IMAGE = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-server_qemu-x86_64"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "upstream_server_cpu.json").write_text(json.dumps(payload, indent=2) + "\n")


def _blocker(status: str, next_step: str) -> int:
    _write({
        "schema": "llama/upstream-server.v2", "evidence_id": "llama-upstream-server-cpu",
        "status": status, "pass": False, "generated_utc": _now(),
        "claim_allowed": "Blocker documented; no server-runtime claim.",
        "claim_forbidden": "HTTP throughput or runtime claim without a same-run READY marker.",
        "next_step": next_step,
    })
    print(f"llama-server-cpu-capture: {status}")
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
        return _blocker("blocked:qemu-missing", "Install qemu-system-x86 and rerun.")
    if not IMAGE.exists():
        return _blocker("blocked:unikraft-image-missing",
                        "Build kraft/Kraftfile.llama-upstream-server then rerun.")
    model = _model_path()
    if model is None:
        return _blocker("blocked:model-missing", "Set VOGUE_CPU_MODEL=/path/to/model.gguf and rerun.")

    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")

    with tempfile.TemporaryDirectory(prefix="vogue-srv-model-") as share:
        shutil.copy(model, Path(share) / "model.gguf")
        cmd = [
            qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", "4096",
            "-nographic", "-no-reboot", "-kernel", str(IMAGE),
            "-fsdev", f"local,id=myid,path={share},security_model=none",
            "-device", "virtio-9p-pci,fsdev=myid,mount_tag=model",
            "-append", "console=ttyS0",
        ]
        # Server stays alive after READY; bound the boot and capture what it printed.
        def _dec(x) -> str:
            if x is None:
                return ""
            return x.decode("utf-8", "replace") if isinstance(x, bytes) else x
        try:
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=120)
            out = proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as e:
            out = _dec(e.stdout) + _dec(e.stderr)

    (RESULTS / "upstream_server_cpu.log").write_text(out)
    (RESULTS / "upstream_server_cpu_serial.log").write_text(out)
    m = re.search(r"uk-llama-upstream-server: READY ([^\n]+)", out)
    if not m:
        return _blocker("blocked:no-pass-line",
                        "Inspect results/llama/upstream_server_cpu.log; no READY marker observed.")

    _write({
        "schema": "llama/upstream-server.v2", "evidence_id": "llama-upstream-server-cpu",
        "status": "pass", "pass": True, "scaffold_booted": True, "generated_utc": _now(),
        "ready_line": m.group(0).strip(),
        "accel": accel, "cpu": cpu, "host": platform.platform(), "model": model.name,
        "run_log": "results/llama/upstream_server_cpu.log",
        "claim_allowed": (f"Upstream llama.cpp server appliance boots directly into a single entrypoint on "
                          f"Unikraft (no shell/fork/exec) and reaches READY on this host ({accel}/-cpu {cpu})."),
        "claim_forbidden": "HTTP throughput or request/response benchmarking until the lwIP netdev gate lands.",
    })
    print(f"llama-server-cpu-capture: pass ({m.group(0).strip()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
