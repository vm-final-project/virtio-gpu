#!/usr/bin/env python3
"""Run the Vulkan llama.cpp bench or HTTP server appliance under QEMU."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

from common import (
    acceleration,
    default_console,
    default_qemu_binary,
    file_size,
    image_suffix,
    machine_and_cpu_args,
    normalize_arch,
    peak_child_rss_kb,
    resolve_model,
    resolve_qemu,
    result,
    result_path,
    run_timed,
    write_json,
)

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/llama"


def image(mode: str, arch: str) -> Path:
    name = "vogue-llama-vk" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_{image_suffix(arch)}"


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, port: int, arch: str) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    # Hosts without a GPU render node (renderD*) can point egl-headless at a
    # primary KMS node driven by software (e.g. VOGUE_EGL_RENDERNODE=/dev/dri/card0
    # with MESA_LOADER_DRIVER_OVERRIDE=kms_swrast). Empty -> let QEMU auto-scan.
    egl_display = "egl-headless,gl=on"
    rendernode = os.environ.get("VOGUE_EGL_RENDERNODE")
    if rendernode:
        egl_display += f",rendernode={rendernode}"
    command = [
        # The VOGUE unikernel is single-core only, so -smp is pinned to 1; this is
        # the apples-to-apples point the vogue-baselines 1-core variants compare
        # against. -m 8192 matches the guest RAM budget given to the Linux/microvm
        # baselines (QEMU does not pre-allocate, so the ceiling does not inflate
        # peak RSS); the GPU blob window (hostmem) stays appliance-specific below.
        qemu, *machine_and_cpu_args(arch, accel), "-smp", "1", "-m", "8192",
        "-no-reboot", "-kernel", str(image(mode, arch)),
        "-display", egl_display, "-vga", "none",
        "-device", "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
        "-append", f"console={default_console(arch)}", "-serial", "mon:stdio", "-monitor", "none",
    ]
    if mode == "server":
        command += [
            "-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{port}-10.0.2.15:8080",
            "-device", "virtio-net-pci,netdev=net0",
        ]
    return command


def http_metrics(port: int, deadline: float, launch: float) -> dict:
    health = f"http://127.0.0.1:{port}/health"
    while time.monotonic() < deadline:
        try:
            started = time.monotonic()
            with urllib.request.urlopen(health, timeout=2) as response:
                body = response.read().decode("utf-8", "replace")
            metrics = {
                "http_status": response.status,
                "health": json.loads(body),
                "latency_s": round(time.monotonic() - started, 4),
                # First successful /health: the server is up and model-ready, so
                # this is the launch->ready boot time for the VK server appliance.
                "boot_time_s": round(time.monotonic() - launch, 3),
            }
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/completion",
                data=json.dumps({"prompt": "Hello", "n_predict": 8}).encode(),
                headers={"Content-Type": "application/json"},
            )
            started = time.monotonic()
            with urllib.request.urlopen(request, timeout=deadline - time.monotonic()) as response:
                completion = json.loads(response.read().decode("utf-8", "replace"))
            elapsed = max(time.monotonic() - started, 0.0001)
            tokens = completion.get("tokens_predicted", 0)
            metrics.update({
                "completion_status": response.status,
                "requests_per_s": round(1 / elapsed, 3),
                "tokens_per_s": round(tokens / elapsed, 3),
            })
            return metrics
        except Exception:
            time.sleep(0.5)
    return {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else ''}vk.json", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    port = 0
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, port, arch)
    inputs = {"mode": args.mode, "arch": arch, "model": str(args.model), "image": str(image(args.mode, arch))}
    blocker = (
        ("blocked:qemu-missing", "QEMU executable not found") if not qemu else
        ("blocked:model-missing", "Model file not found") if not model else
        ("blocked:image-missing", "Unikraft image not found") if not image(args.mode, arch).is_file() else
        None
    )
    if blocker:
        write_json(output, result(blocker[0], base, inputs=inputs, error=blocker[1]))
        print(f"llama-vk-{args.mode}: {blocker[0]}")
        return 0
    if args.mode == "server":
        port = free_port()
        base = qemu_command(qemu, args.model, args.mode, args.timeout, port, arch)

    with tempfile.TemporaryDirectory(prefix="vogue-model-") as directory:
        shutil.copy(model, Path(directory) / "model.gguf")
        command = [
            *base,
            "-fsdev", f"local,id=model,path={directory},security_model=none",
            "-device", "virtio-9p-pci,fsdev=model,mount_tag=model",
        ]
        if args.mode == "server":
            launch = time.monotonic()
            proc = subprocess.Popen(command, cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                metrics = http_metrics(port, time.monotonic() + args.timeout, launch)
                proc.terminate()
                log, _ = proc.communicate(timeout=10)
            except Exception:
                proc.kill()
                log, _ = proc.communicate()
                metrics = {}
            ready = "uk-llama-upstream-vk-server: READY" in log
            passed = (
                ready
                and metrics.get("http_status") == 200
                and metrics.get("completion_status") == 200
            )
            metrics["ready"] = ready
            # proc is reaped by communicate() above, so getrusage has its peak.
            metrics["peak_rss_kb"] = peak_child_rss_kb()
        else:
            # Stream serial output so the unikernel's "config" line can be
            # timed: it prints after boot + Vulkan/Venus dispatch init + 9p
            # mount, just before llama-bench, giving the launch->ready boot
            # time separate from the pp512/tg128 inference numbers.
            run = run_timed(command, cwd=ROOT, timeout=args.timeout,
                            markers={"boot": "uk-llama-upstream-vk: config"})
            log = run.text
            # The bench appliance runs upstream llama-bench, which prints a
            # markdown table; pull pp512/tg128 t/s from the test-column rows
            # (e.g. "| ... | pp512 | 4582.61 ± 12.34 |"). Same shape the
            # vogue-baselines runners parse.
            pp = re.search(r"\|\s*pp512\s*\|\s*([0-9.]+)", log)
            tg = re.search(r"\|\s*tg128\s*\|\s*([0-9.]+)", log)
            passed = bool(pp and tg and "PASS" in log)
            metrics = {"boot_time_s": run.elapsed("boot"),
                       "peak_rss_kb": run.peak_rss_kb}
            if pp and tg:
                metrics["pp512"] = float(pp.group(1))
                metrics["tg128"] = float(tg.group(1))

    # Footprint metrics common to both modes: bootable image size and, where the
    # appliance loads via load_model_common (server), its timed weight load.
    # (Bench runs upstream llama-bench, which bundles load, so it has no line.)
    metrics["image_bytes"] = file_size(image(args.mode, arch))
    load = re.search(r"model_load .*elapsed_ms=([0-9.]+)", log)
    metrics["model_load_ms"] = float(load.group(1)) if load else None

    status = "pass" if passed else "blocked:no-pass-marker"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-vk-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
