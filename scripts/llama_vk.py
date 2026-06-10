#!/usr/bin/env python3
"""Run the Vulkan llama.cpp bench or HTTP server appliance under QEMU."""
from __future__ import annotations

import argparse
import json
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
    decode,
    default_console,
    default_qemu_binary,
    image_suffix,
    machine_and_cpu_args,
    normalize_arch,
    resolve_model,
    resolve_qemu,
    result,
    result_path,
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
    command = [
        qemu, *machine_and_cpu_args(arch, accel), "-m", "3072",
        "-no-reboot", "-kernel", str(image(mode, arch)),
        "-display", "egl-headless,gl=on", "-vga", "none",
        "-device", "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
        "-append", f"console={default_console(arch)}", "-serial", "mon:stdio", "-monitor", "none",
    ]
    if mode == "server":
        command += [
            "-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{port}-10.0.2.15:8080",
            "-device", "virtio-net-pci,netdev=net0",
        ]
    return command


def http_metrics(port: int, deadline: float) -> dict:
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
            proc = subprocess.Popen(command, cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                metrics = http_metrics(port, time.monotonic() + args.timeout)
                proc.terminate()
                log, _ = proc.communicate(timeout=10)
            except Exception:
                proc.kill()
                log, _ = proc.communicate()
                metrics = {}
            ready = "uk-llama-vk-server: READY" in log
            passed = (
                ready
                and metrics.get("http_status") == 200
                and metrics.get("completion_status") == 200
            )
            metrics["ready"] = ready
        else:
            try:
                proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                      timeout=args.timeout, check=False)
                log = proc.stdout + proc.stderr
            except subprocess.TimeoutExpired as exc:
                log = decode(exc.stdout) + decode(exc.stderr)
            match = re.search(r"pp512=([0-9.]+)\s+tg128=([0-9.]+)", log)
            passed = bool(match and "PASS" in log)
            metrics = (
                {"pp512": float(match.group(1)), "tg128": float(match.group(2))}
                if match else {}
            )

    status = "pass" if passed else "blocked:no-pass-marker"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-vk-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
