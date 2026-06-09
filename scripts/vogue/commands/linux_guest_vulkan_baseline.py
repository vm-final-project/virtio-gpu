#!/usr/bin/env python3
"""QEMU + normal-Linux-kernel + Vulkan(Venus) baseline for llama.cpp.

This is the apples-to-apples para-virtualised reference for the Unikraft
`llm.bench.vk` / `llm.server.vk` rows: the SAME QEMU `virtio-gpu-gl,venus=true`
transport and the SAME upstream ggml-vulkan binary + GGUF, but inside a stock
Linux guest kernel instead of the Unikraft unikernel. It isolates the
unikernel-vs-Linux difference from the (separately measured) virtualised-vs-bare-
metal difference (`vulkan_linux_baseline.json`).

Method (no disk image needed):
  * Build a tiny static-busybox initramfs that brings up 9p, then mounts the
    host root read-only over virtio-9p, loads the host's virtio-gpu DRM module,
    and runs the host's `build-vk/bin/llama-bench` against the Mesa Venus guest
    ICD (`virtio_icd.json` / `libvulkan_virtio.so`) over the virtio-gpu device.
  * The guest reuses the host kernel (`/boot/vmlinuz-$(uname -r)`), modules, and
    Vulkan userspace through the 9p mount — nothing is copied except the few 9p
    bootstrap modules.

Writes results/llama/vulkan_qemu_linux_baseline.json. Hosts without the kernel,
KVM, GPU, or model emit a structured blocker (parity with the other gates).
"""
from __future__ import annotations

import gzip
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / "results" / "llama"
OUT = RESULTS / "vulkan_qemu_linux_baseline.json"
SERIAL = RESULTS / "vulkan_qemu_linux_baseline_serial.log"

LLAMA_BENCH = ROOT.parent / "llama.cpp" / "build-vk" / "bin" / "llama-bench"
KREL = platform.release()
KERNEL = Path(f"/boot/vmlinuz-{KREL}")
MODDIR = Path(f"/lib/modules/{KREL}")
BUSYBOX = Path(shutil.which("busybox") or "/usr/bin/busybox")

# 9p bootstrap modules must live in the initramfs (needed before the host mount).
BOOT_MODULES = [
    MODDIR / "kernel/fs/netfs/netfs.ko",
    MODDIR / "kernel/net/9p/9pnet.ko",
    MODDIR / "kernel/net/9p/9pnet_virtio.ko",
    MODDIR / "kernel/fs/9p/9p.ko",
]
# virtio-gpu DRM modules — loaded from the host tree after the 9p mount.
GPU_MODULES = [
    "/host/lib/modules/%s/kernel/drivers/virtio/virtio_dma_buf.ko" % KREL,
    "/host/lib/modules/%s/kernel/drivers/gpu/drm/virtio/virtio-gpu.ko" % KREL,
]

BEGIN, END = "VK-LINUX-GUEST-BEGIN", "VK-LINUX-GUEST-END"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _model() -> Path | None:
    for env in ("VOGUE_VK_MODEL", "VOGUE_CPU_MODEL"):
        v = os.environ.get(env)
        if v and Path(v).is_file():
            return Path(v)
    for p in (ROOT.parent / "models").rglob("*.gguf"):
        return p
    return None


def _select_qemu() -> str | None:
    cands = [os.environ.get("QEMU"),
             str(ROOT.parent / "qemu-src" / "build" / "qemu-system-x86_64"),
             shutil.which("qemu-system-x86_64")]
    for c in [c for c in cands if c and Path(c).exists()]:
        try:
            out = subprocess.run([c, "-device", "virtio-gpu-gl-pci,help"],
                                 text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=10).stdout
            if "venus=" in out:
                return c
        except Exception:  # noqa: BLE001
            continue
    cands = [c for c in cands if c and Path(c).exists()]
    return cands[0] if cands else None


def _grant_render_nodes() -> None:
    nodes = [str(p) for p in Path("/dev/dri").glob("renderD*")]
    if nodes and not all(os.access(n, os.R_OK | os.W_OK) for n in nodes) and shutil.which("sudo"):
        subprocess.run(["sudo", "-n", "chmod", "o+rw", *nodes],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _emit(status: str, extra: dict | None = None) -> int:
    payload = {
        "schema": "llama/vulkan-qemu-linux-baseline.v1",
        "evidence_id": "llama-vk-qemu-linux",
        "generated_utc": _now(),
        "status": status,
        "pass": status == "pass",
        "host": platform.platform(),
        "guest_kernel": str(KERNEL) if KERNEL.exists() else None,
        "backend": "Vulkan via Mesa Venus guest ICD (virtio_icd) over QEMU virtio-gpu-gl venus=true; stock Linux guest kernel",
        "transport": "Linux guest DRM virtio-gpu -> QEMU virtio-gpu-gl venus=true -> host virglrenderer Venus",
        "claim_allowed": ("Para-virtualised Vulkan baseline: stock Linux guest kernel in QEMU over the same "
                          "virtio-gpu-gl Venus path and the same upstream ggml-vulkan binary/GGUF as the "
                          "Unikraft port. Isolates unikernel-vs-Linux from virtualised-vs-bare-metal."),
        "claim_forbidden": "Bare-metal-native comparison (that is vulkan_linux_baseline.json) or cross-host claim.",
    }
    if extra:
        payload.update(extra)
    RESULTS.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(payload, indent=2) + "\n")
    msg = f"linux-guest-vk-baseline: {status}"
    if status == "pass":
        msg += f" pp512={payload.get('pp512_t_per_s')} tg128={payload.get('tg128_t_per_s')} dev={payload.get('device')}"
    print(msg)
    return 0


def _build_initramfs(workdir: Path, model: Path) -> Path:
    root = workdir / "irfs"
    for d in ("bin", "modules", "proc", "sys", "dev", "tmp", "host"):
        (root / d).mkdir(parents=True)
    shutil.copy(BUSYBOX, root / "bin" / "busybox")
    os.chmod(root / "bin" / "busybox", 0o755)
    for m in BOOT_MODULES:
        shutil.copy(m, root / "modules" / m.name)

    gpu_insmods = "\n".join(f"insmod {m}" for m in GPU_MODULES)
    init = f"""#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev 2>/dev/null
mount -t tmpfs tmpfs /tmp
insmod /modules/netfs.ko
insmod /modules/9pnet.ko
insmod /modules/9pnet_virtio.ko
insmod /modules/9p.ko
mkdir -p /host
mount -t 9p -o trans=virtio,version=9p2000.L,ro,msize=262144 hostfs /host
# The host llama-bench is a dynamic PIE; expose the host's loader + libraries so
# its hardcoded interpreter (/lib64/ld-linux-x86-64.so.2) resolves.
ln -s /host/lib64 /lib64
ln -s /host/lib /lib
ln -s /host/usr /usr
{gpu_insmods}
# settle udev-less devtmpfs render node
i=0; while [ ! -e /dev/dri/renderD128 ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
export LD_LIBRARY_PATH=/host/usr/lib/x86_64-linux-gnu:/host{LLAMA_BENCH.parent}
export VK_ICD_FILENAMES=/host/usr/share/vulkan/icd.d/virtio_icd.json
echo "dri-nodes: $(ls /dev/dri 2>/dev/null)"
# Write JSON to a guest tmpfs file so the verbose host virgl_render_server debug
# (interleaved on the captured stream) does not corrupt llama-bench's output;
# emit it cleanly between markers only after the run completes.
/host{LLAMA_BENCH} -m /host{model} -ngl 99 -p 512 -n 128 -o json >/tmp/vkguest.json 2>/tmp/vkguest.err
echo "{BEGIN}"
cat /tmp/vkguest.json
echo "{END}"
echo "--- stderr tail ---"
tail -n 8 /tmp/vkguest.err
poweroff -f
"""
    (root / "init").write_text(init)
    os.chmod(root / "init", 0o755)

    # pack cpio.gz
    names = subprocess.run(["find", ".", "-print0"], cwd=root, check=True,
                           stdout=subprocess.PIPE).stdout
    cpio = subprocess.run(["cpio", "-o", "-H", "newc", "--null"], cwd=root,
                          input=names, stdout=subprocess.PIPE, check=True).stdout
    out = workdir / "initramfs.cpio.gz"
    out.write_bytes(gzip.compress(cpio))
    return out


def _parse(out: str) -> dict | None:
    seg = out.split(BEGIN, 1)
    if len(seg) < 2:
        return None
    body = seg[1].split(END, 1)[0]
    m = re.search(r"(\[\s*\{.*\}\s*\])", body, re.DOTALL)
    if not m:
        return None
    try:
        rows = json.loads(m.group(1))
    except json.JSONDecodeError:
        return None
    res: dict = {}
    dev = None
    for r in rows:
        dev = r.get("gpu_info") or r.get("backend_name") or dev
        ts = r.get("avg_ts")
        if r.get("n_prompt") and not r.get("n_gen"):
            res["pp512_t_per_s"] = round(float(ts), 2)
        elif r.get("n_gen") and not r.get("n_prompt"):
            res["tg128_t_per_s"] = round(float(ts), 2)
    if dev:
        res["device"] = dev
    return res or None


def main() -> int:
    if not LLAMA_BENCH.exists():
        return _emit("blocked:llama-bench-missing", {"detail": str(LLAMA_BENCH)})
    if not KERNEL.exists() or not os.access(KERNEL, os.R_OK):
        return _emit("blocked:guest-kernel-unreadable", {"detail": str(KERNEL)})
    if not all(m.exists() for m in BOOT_MODULES):
        return _emit("blocked:9p-modules-missing")
    if not BUSYBOX.exists():
        return _emit("blocked:busybox-missing")
    qemu = _select_qemu()
    if not qemu:
        return _emit("blocked:qemu-missing")
    model = _model()
    if not model:
        return _emit("blocked:model-missing")

    _grant_render_nodes()
    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")
    timeout = int(os.environ.get("VOGUE_VKGUEST_TIMEOUT", "600"))

    with tempfile.TemporaryDirectory(prefix="vogue-vkguest-") as wd:
        initramfs = _build_initramfs(Path(wd), model)
        cmd = [
            qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m",
            os.environ.get("VOGUE_VKGUEST_MEM", "4096"), "-no-reboot",
            "-kernel", str(KERNEL), "-initrd", str(initramfs),
            "-append", "console=ttyS0 rdinit=/init",
            "-fsdev", "local,id=hostfs,path=/,security_model=none,readonly=on",
            "-device", "virtio-9p-pci,fsdev=hostfs,mount_tag=hostfs",
            "-display", "egl-headless,gl=on", "-vga", "none",
            "-device", "virtio-gpu-gl-pci,hostmem=8G,blob=true,venus=true",
            "-serial", "mon:stdio", "-monitor", "none",
        ]
        env = {**os.environ, "VIRGL_DEBUG": os.environ.get("VIRGL_DEBUG", "")}
        try:
            proc = subprocess.run(cmd, cwd=ROOT, text=True, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  timeout=timeout)
            out = proc.stdout
        except subprocess.TimeoutExpired as e:
            out = (e.stdout or "") if isinstance(e.stdout, str) else (e.output or "")
        except OSError as e:
            return _emit("blocked:qemu-spawn-failed", {"error": str(e)})

    SERIAL.parent.mkdir(parents=True, exist_ok=True)
    SERIAL.write_text(out)
    parsed = _parse(out)
    if parsed and parsed.get("tg128_t_per_s"):
        return _emit("pass", {**parsed, "llama_bench": str(LLAMA_BENCH),
                              "model_filename": model.name, "n_gpu_layers": 99})
    tail = out[-3000:]
    if "Unikraft Crash" in out or "Kernel panic" in out:
        return _emit("blocked:guest-panic", {"log_tail": tail})
    return _emit("blocked:no-bench-output", {"log_tail": tail})


if __name__ == "__main__":
    raise SystemExit(main())
