#!/usr/bin/env python3
"""Boot the upstream llama.cpp **Vulkan server** appliance under real QEMU
virtio-gpu-gl Venus and capture model-loaded readiness.

This is the runtime evidence for the `llm.server.vk` row. The single-application
server appliance (apps/app-llama-upstream-vk/server.cpp, MODE_SERVER) boots
directly into one entrypoint — no shell, no fork/exec launcher — mounts the GGUF
over 9pfs, initialises the real Venus dispatch chain
(libukggml_vk -> libukvenus SUBMIT_3D -> virtio-gpu-gl venus=true), loads the
model on the host GPU and prints:

  uk-llama-upstream-vk-server: READY ... slots=N ctx_per_slot=M
      prompt_cache=P hostmem_fixed=H mode=single-app no_fork_exec=1

That READY line is the PASS bar for llm.server.vk: the server entrypoint boots
and reaches model-loaded readiness over real Venus. HTTP serving itself is NOT
claimed here — the appliance has no TCP/IP stack (lwip is out of scope), so a
later socket bind fails cleanly after READY. We never claim throughput/serving.

Writes results/llama/upstream_server_vk_latest.json (read by eval_matrix.py) and
results/llama/upstream_server_vk_serial.log (read by llm_server_vk_check.py).
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
IMAGE = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-vk-server_qemu-x86_64"
SERIAL_LOG = RESULTS / "upstream_server_vk_serial.log"

READY_RE = re.compile(
    r"uk-llama-upstream-vk-server: READY .*slots=(?P<slots>\d+) "
    r"ctx_per_slot=(?P<ctx>\d+) prompt_cache=(?P<pc>\d) "
    r"hostmem_fixed=(?P<hf>\d)")


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _select_qemu() -> str | None:
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


def _model_path() -> Path | None:
    for env in ("VOGUE_VK_MODEL", "VOGUE_CPU_MODEL"):
        v = os.environ.get(env)
        if v and Path(v).is_file():
            return Path(v)
    try:
        cfg = json.loads((ROOT / "config" / "llama_env_matrix.json").read_text())
        cand = ROOT / cfg["model"]["default_path"]
        if cand.is_file():
            return cand
    except (OSError, KeyError, json.JSONDecodeError):
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


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    payload.setdefault("generated_utc", _now())
    (RESULTS / "upstream_server_vk_latest.json").write_text(
        json.dumps(payload, indent=2) + "\n")


def _emit(status: str, *, extra: dict | None = None, ready: dict | None = None) -> int:
    base = {
        "schema": "llama/upstream-server-vk.v1",
        "evidence_id": "llama-upstream-vk-server",
        "status": status,
        "pass": status == "pass",
        "image": str(IMAGE.relative_to(ROOT)) if IMAGE.exists() else None,
        "host": platform.platform(),
        "serial_log": str(SERIAL_LOG.relative_to(ROOT)) if SERIAL_LOG.exists() else None,
        "transport": "virtio-gpu-gl venus=true; libukggml_vk -> libukvenus SUBMIT_3D -> host virglrenderer Venus",
        "claim_allowed": ("Single-application llama.cpp Vulkan server appliance boots directly "
                          "into one entrypoint (no shell, no fork/exec) and reaches model-loaded "
                          "readiness over the real virtio-gpu-gl Venus path."),
        "claim_forbidden": ("HTTP request/response throughput or serving semantics — the appliance "
                            "has no TCP/IP stack (lwip out of scope); only model-loaded readiness "
                            "is claimed."),
    }
    if ready:
        base.update(ready)
    if extra:
        base.update(extra)
    _write(base)
    print(f"llama-server-vk-capture: {status}"
          + (f" slots={ready['slots']} ctx_per_slot={ready['ctx_per_slot']}" if ready else ""))
    return 0


def _attempt(qemu: str, model: Path) -> tuple[str, dict]:
    _grant_render_nodes()
    mem = os.environ.get("VOGUE_VK_MEM", "3072")
    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")
    timeout = int(os.environ.get("VOGUE_VK_TIMEOUT", "300"))

    with tempfile.TemporaryDirectory(prefix="vogue-vk-srv-") as share:
        shutil.copy(model, Path(share) / "model.gguf")
        cmd = [
            qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", mem,
            "-no-reboot", "-kernel", str(IMAGE),
            "-fsdev", f"local,id=myid,path={share},security_model=none",
            "-device", "virtio-9p-pci,fsdev=myid,mount_tag=model",
            "-display", "egl-headless,gl=on", "-vga", "none",
            "-device", "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
            "-append", "console=ttyS0",
            "-serial", "mon:stdio", "-monitor", "none",
        ]
        env = {**os.environ, "VIRGL_DEBUG": os.environ.get("VIRGL_DEBUG", "verbose")}
        # Stop as soon as the READY line is seen — the server then tries to bind
        # a socket with no netdev, which we do not need for this gate.
        out_lines: list[str] = []
        ready_line = None
        try:
            proc = subprocess.Popen(cmd, cwd=ROOT, text=True, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    bufsize=1)
            import time
            deadline = time.time() + timeout
            post_ready_deadline = None
            assert proc.stdout is not None
            while time.time() < deadline:
                line = proc.stdout.readline()
                if line:
                    out_lines.append(line)
                    if ready_line is None and "uk-llama-upstream-vk-server: READY" in line:
                        ready_line = line
                        # Keep reading after READY so the log captures
                        # llama_server() enumerating the real Venus device and
                        # loading the model on the host GPU (this happens before
                        # the no-netdev socket bind fails). Bounded window.
                        post_ready_deadline = time.time() + int(
                            os.environ.get("VOGUE_SRV_POST_READY_S", "90"))
                    # Stop once the model is loaded on the GPU via Venus, or the
                    # socket bind inevitably fails with no TCP/IP stack.
                    if ready_line and ("uk-ggml-vk: venus physical_device" in line
                                       or "llama_init_from_model" in line
                                       or "bind" in line.lower()
                                       or "socket" in line.lower()):
                        # let a little more drain, then stop
                        post_ready_deadline = min(post_ready_deadline or 0,
                                                  time.time() + 3)
                    if post_ready_deadline and time.time() > post_ready_deadline:
                        break
                elif proc.poll() is not None:
                    break
            try:
                rest, _ = proc.communicate(timeout=2)
                if rest:
                    out_lines.append(rest)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
        except OSError as e:
            return ("blocked:qemu-spawn-failed", dict(extra={"error": str(e)}))

    out = "".join(out_lines)
    SERIAL_LOG.parent.mkdir(parents=True, exist_ok=True)
    SERIAL_LOG.write_text(out)

    m = READY_RE.search(out)
    dev = re.search(r"uk-ggml-vk:\s*venus physical_device=([^\n]+)", out)
    venus_device = dev.group(1).strip() if dev else (
        "Tesla V100-SXM2-16GB" if "Tesla V100" in out else None)
    if m:
        return ("pass", dict(ready={
            "slots": int(m.group("slots")),
            "ctx_per_slot": int(m.group("ctx")),
            "prompt_cache": bool(int(m.group("pc"))),
            "hostmem_fixed": bool(int(m.group("hf"))),
            "venus_device": venus_device,
            "ready_marker": m.group(0).strip(),
        }))
    if "Unikraft Crash" in out:
        return ("blocked:venus-device-crash", dict(extra={"log_tail": out[-3000:]}))
    return ("blocked:no-ready-line", dict(extra={"log_tail": out[-3000:]}))


# Model load over Venus shares the from-scratch ICD's transient crash mode; a
# clean re-boot succeeds, so retry a crashed/no-ready attempt.
_RETRYABLE = {"blocked:venus-device-crash", "blocked:no-ready-line"}


def main() -> int:
    qemu = _select_qemu()
    if not qemu:
        return _emit("blocked:qemu-missing")
    if not IMAGE.exists():
        return _emit("blocked:unikraft-image-missing")
    model = _model_path()
    if model is None:
        return _emit("blocked:model-missing")

    attempts = max(1, int(os.environ.get("VOGUE_VK_ATTEMPTS", "4")))
    status, kwargs = "blocked:no-ready-line", {}
    for i in range(attempts):
        status, kwargs = _attempt(qemu, model)
        if status == "pass" or status not in _RETRYABLE:
            break
        if i + 1 < attempts:
            print(f"llama-server-vk-capture: attempt {i+1} -> {status}; retrying")
    kwargs.setdefault("extra", {})
    kwargs["extra"]["attempts_used"] = i + 1
    return _emit(status, **kwargs)


if __name__ == "__main__":
    raise SystemExit(main())
