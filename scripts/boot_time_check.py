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

# Per-appliance boot configuration. Appliances are single-purpose and need the
# devices/model their entrypoint expects, otherwise they never reach READY:
#   gpu=True   -> attach virtio-gpu-gl-pci,venus=true,blob=true + egl-headless
#                 (the kmscube/Vulkan scanout + Venus compute path)
#   model=True -> stage the GGUF over virtio-9p (mount_tag=model) like the real
#                 runtime scripts do; the llama entrypoints mount /mnt/model
#   mem        -> guest RAM (vk needs the hostmem BAR to map in the <4G hole)
APPLIANCES = [
    {"name": "kmscube",          "image": "vogue_qemu-x86_64",
     "ready": re.compile(r"uk-kmscube: PASS"), "gpu": True, "mem": "512M"},
    {"name": "glmark2",          "image": "vogue-glmark2_qemu-x86_64",
     "ready": re.compile(r"uk-glmark2: PASS|status=pass"), "gpu": True, "mem": "512M"},
    {"name": "llama-cpu-bench",  "image": "vogue-llama-upstream-cpu_qemu-x86_64",
     "ready": re.compile(r"uk-llama-upstream: PASS"), "model": True, "mem": "2048"},
    {"name": "llama-cpu-server", "image": "vogue-llama-upstream-server_qemu-x86_64",
     "ready": re.compile(r"uk-llama-upstream-server: READY"), "model": True, "mem": "2048"},
    {"name": "llama-vk-bench",   "image": "vogue-llama-upstream-vk_qemu-x86_64",
     "ready": re.compile(r"uk-llama-upstream-vk: PASS"), "gpu": True, "model": True, "mem": "3072"},
    {"name": "llama-vk-server",  "image": "vogue-llama-upstream-vk-server_qemu-x86_64",
     "ready": re.compile(r"uk-llama-upstream-vk-server: READY"), "gpu": True, "model": True, "mem": "3072"},
]

# vk model load on the V100 over Venus takes several seconds; give GPU/model
# appliances a generous boot-to-READY budget while keeping CPU ones snappy.
DEFAULT_TIMEOUT_S = 30.0
GPU_TIMEOUT_S = 360.0


def _qemu_bin() -> str | None:
    """Prefer a Venus-capable qemu (auto-selects the qemu-src build) so the GPU
    appliances can reach the host GPU; fall back to $QEMU / PATH."""
    explicit = os.environ.get("QEMU")
    cands = []
    if explicit:
        cands.append(shutil.which(explicit) or explicit)
    cands += [str(ROOT.parent / "qemu-src" / "build" / "qemu-system-x86_64"),
              "/usr/local/bin/qemu-system-x86_64"]
    path_bin = shutil.which("qemu-system-x86_64")
    if path_bin:
        cands.append(path_bin)
    existing = [c for c in cands if c and Path(c).exists()]
    for c in existing:
        try:
            out = subprocess.run([c, "-device", "virtio-gpu-gl-pci,help"],
                                 text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=10).stdout
        except Exception:
            continue
        if "venus=" in out:
            return c
    return existing[0] if existing else None


def _model_file() -> Path | None:
    import json as _json
    for env in ("VOGUE_VK_MODEL", "VOGUE_CPU_MODEL"):
        v = os.environ.get(env)
        if v and Path(v).is_file():
            return Path(v)
    try:
        cfg = _json.loads((ROOT / "config" / "llama_env_matrix.json").read_text())
        cand = ROOT / cfg["model"]["default_path"]
        if cand.is_file():
            return cand
    except Exception:
        pass
    for p in (ROOT.parent / "models").rglob("*.gguf"):
        return p
    return None


def _grant_render_nodes() -> None:
    nodes = [str(p) for p in Path("/dev/dri").glob("renderD*")]
    if not nodes or all(os.access(n, os.R_OK | os.W_OK) for n in nodes):
        return
    if shutil.which("sudo"):
        subprocess.run(["sudo", "-n", "chmod", "o+rw", *nodes],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _measure(app: dict, timeout_s: float) -> dict:
    image_path = BUILD / app["image"]
    qemu = _qemu_bin()
    if qemu is None:
        return {"status": "blocked:qemu-missing"}
    if not image_path.exists():
        return {"status": "blocked:image-missing", "image": str(image_path.relative_to(ROOT))}

    needs_gpu = bool(app.get("gpu"))
    needs_model = bool(app.get("model"))
    mem = app.get("mem", "256M")
    # Use KVM whenever available: the bench appliances run a full pp512+tg128
    # inference before their PASS marker, which is far too slow under TCG.
    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")

    share = None
    if needs_model:
        model = _model_file()
        if model is None:
            return {"status": "blocked:model-missing", "image": str(image_path.relative_to(ROOT))}
        import tempfile
        share = tempfile.mkdtemp(prefix="vogue-boot-model-")
        shutil.copy(model, Path(share) / "model.gguf")

    if needs_gpu:
        _grant_render_nodes()

    cmd = [qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", mem,
           "-no-reboot", "-kernel", str(image_path)]
    if needs_model:
        cmd += ["-fsdev", f"local,id=myid,path={share},security_model=none",
                "-device", "virtio-9p-pci,fsdev=myid,mount_tag=model"]
    if needs_gpu:
        cmd += ["-display", "egl-headless,gl=on", "-vga", "none",
                "-device", "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
                "-append", "console=ttyS0"]
    else:
        cmd += ["-display", "none", "-nographic"]
    cmd += ["-serial", "mon:stdio", "-monitor", "none"]

    ready = app["ready"]
    env = {**os.environ}
    if needs_gpu:
        env.setdefault("LD_LIBRARY_PATH", "/usr/local/lib/x86_64-linux-gnu")
        env["VIRGL_DEBUG"] = env.get("VIRGL_DEBUG", "")
    started_at = time.perf_counter()
    proc = subprocess.Popen(
        cmd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1, cwd=ROOT, env=env,
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
        if share:
            shutil.rmtree(share, ignore_errors=True)

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
        # GPU/model appliances load a model on the V100 over Venus before READY,
        # so they need a longer budget than the default CPU boot timeout.
        per_to = GPU_TIMEOUT_S if (a.get("gpu") or a.get("model")) else args.timeout
        result = _measure(a, per_to)
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
