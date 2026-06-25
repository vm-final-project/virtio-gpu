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
    smp_args,
    write_json,
)

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/llama"
SOFTWARE_RENDERER_PATTERNS = ("llvmpipe", "lavapipe", "softpipe", "swrast", "kms_swrast")


def classify_renderer_text(text: str) -> dict:
    collapsed = " ".join(text.split())
    if not collapsed:
        return {
            "host_renderer_name": "unknown",
            "host_renderer_type": "unknown",
        }
    lower = collapsed.lower()
    renderer_type = (
        "software"
        if any(token in lower for token in SOFTWARE_RENDERER_PATTERNS)
        else "hardware"
    )
    return {
        "host_renderer_name": collapsed[:240],
        "host_renderer_type": renderer_type,
    }


def renderer_expect_status(inputs: dict) -> str | None:
    expect = inputs.get("renderer_expect", "any")
    actual = inputs.get("host_renderer_type", "unknown")
    if expect == "any" or actual == "unknown":
        return None
    if expect != actual:
        return "blocked:renderer-expect-mismatch"
    return None


def host_renderer_probe(env: dict | None = None) -> dict:
    environ = dict(os.environ)
    if env:
        environ.update(env)

    explicit = environ.get("VOGUE_HOST_RENDERER_NAME")
    if explicit:
        return classify_renderer_text(explicit)

    vulkaninfo = shutil.which(environ.get("VOGUE_VULKANINFO", "vulkaninfo"))
    if vulkaninfo:
        try:
            proc = subprocess.run(
                [vulkaninfo, "--summary"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=8,
                check=False,
                env=environ,
            )
            if proc.stdout:
                return classify_renderer_text(proc.stdout)
        except Exception as exc:
            return {
                "host_renderer_name": "unknown",
                "host_renderer_type": "unknown",
                "host_renderer_probe_error": str(exc),
            }

    return {
        "host_renderer_name": "unknown",
        "host_renderer_type": "unknown",
    }


def image(mode: str, arch: str, bench_kind: str = "hand_rolled_smoke") -> Path:
    if mode == "bench" and bench_kind == "upstream_llama_bench":
        name = "vogue-llama-vk-upstream-bench"
    else:
        name = "vogue-llama-vk" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_{image_suffix(arch)}"


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, port: int,
                 arch: str, smp: int = 1, env: dict | None = None,
                 bench_kind: str = "hand_rolled_smoke") -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    environ = os.environ if env is None else env
    mem_mb = environ.get("VOGUE_QEMU_MEM_MB", "4096")
    hostmem = environ.get("VOGUE_GPU_HOSTMEM", "4G")
    # Hosts without a GPU render node (renderD*) can point egl-headless at a
    # primary KMS node driven by software (e.g. VOGUE_EGL_RENDERNODE=/dev/dri/card0
    # with MESA_LOADER_DRIVER_OVERRIDE=kms_swrast). Empty -> let QEMU auto-scan.
    egl_display = "egl-headless,gl=on"
    rendernode = environ.get("VOGUE_EGL_RENDERNODE")
    if rendernode:
        egl_display += f",rendernode={rendernode}"
    command = [
        qemu, *machine_and_cpu_args(arch, accel), *smp_args(smp), "-m", mem_mb,
        "-no-reboot", "-kernel", str(image(mode, arch, bench_kind)),
        "-display", egl_display, "-vga", "none",
        "-device", f"virtio-gpu-gl-pci,hostmem={hostmem},blob=true,venus=true",
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


def parse_run_log(log: str, mode: str, metrics: dict) -> dict:
    metrics = dict(metrics)
    bench_kind = re.search(r"uk-llama-bench-kind: ([^\s]+)", log)
    if bench_kind:
        metrics["bench_kind"] = bench_kind.group(1)
    for line in log.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        metrics["bench_kind"] = "upstream_llama_bench"
        metrics["backend"] = "vulkan"
        if "n_threads" in row:
            metrics["threads"] = int(row["n_threads"])
        n_prompt = int(row.get("n_prompt", 0))
        n_gen = int(row.get("n_gen", 0))
        avg_ts = float(row.get("avg_ts", 0.0))
        if n_prompt == 512 and n_gen == 0:
            metrics["pp512"] = avg_ts
            metrics["pp"] = avg_ts
        elif n_prompt == 0 and n_gen == 128:
            metrics["tg128"] = avg_ts
            metrics["tg"] = avg_ts

    blocker = None
    for pattern in (
        r"uk-llama-vk-blocked:\s*(blocked:[^\s]+)",
        r"uk-ggml-vk:\s*(blocked:[^\s]+)",
        r"uk-vulkan:\s*(blocked:[^\s]+)",
    ):
        blocker = re.search(pattern, log)
        if blocker:
            break

    if mode == "bench":
        match = re.search(r"pp512=([0-9.]+)\s+tg128=([0-9.]+)", log)
        if match:
            metrics["pp512"] = float(match.group(1))
            metrics["tg128"] = float(match.group(2))
        passed = bool(
            ("PASS" in log)
            and (
                (match is not None)
                or (metrics.get("bench_kind") == "upstream_llama_bench"
                    and "pp512" in metrics and "tg128" in metrics)
            )
        )
    else:
        ready = "uk-llama-upstream-vk-server: READY" in log
        metrics["ready"] = ready
        passed = (
            ready
            and metrics.get("http_status") == 200
            and metrics.get("completion_status") == 200
        )

    if blocker:
        return {
            "passed": False,
            "status": blocker.group(1),
            "metrics": metrics,
        }

    return {
        "passed": passed,
        "status": "pass" if passed else "blocked:no-pass-marker",
        "metrics": metrics,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
    parser.add_argument("--bench-kind", choices=("hand_rolled_smoke", "upstream_llama_bench"),
                        default="hand_rolled_smoke")
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    bench_prefix = "upstream_bench_" if args.mode == "bench" and args.bench_kind == "upstream_llama_bench" else ""
    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else bench_prefix}vk.json", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    port = 0
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout,
                        port, arch, smp=args.smp, bench_kind=args.bench_kind)
    inputs = {
        "mode": args.mode,
        "arch": arch,
        "model": str(args.model),
        "image": str(image(args.mode, arch, args.bench_kind)),
        "smp": args.smp,
        "bench_kind": args.bench_kind,
        "backend": "vulkan",
        "qemu_mem_mb": int(os.environ.get("VOGUE_QEMU_MEM_MB", "4096")),
        "gpu_hostmem": os.environ.get("VOGUE_GPU_HOSTMEM", "4G"),
        "egl_rendernode": os.environ.get("VOGUE_EGL_RENDERNODE", "auto"),
        "renderer_expect": os.environ.get("VOGUE_RENDERER_EXPECT", "any"),
        "model_size_bytes": model.stat().st_size if model else 0,
    }
    inputs.update(host_renderer_probe())
    renderer_blocker = renderer_expect_status(inputs)
    blocker = (
        ("blocked:qemu-missing", "QEMU executable not found") if not qemu else
        ("blocked:model-missing", "Model file not found") if not model else
        (renderer_blocker, "Host renderer did not match VOGUE_RENDERER_EXPECT") if renderer_blocker else
        ("blocked:image-missing", "Unikraft image not found") if not image(args.mode, arch, args.bench_kind).is_file() else
        None
    )
    if blocker:
        write_json(output, result(blocker[0], base, inputs=inputs, error=blocker[1]))
        print(f"llama-vk-{args.mode}: {blocker[0]}")
        return 0
    if args.mode == "server":
        port = free_port()
        base = qemu_command(qemu, args.model, args.mode, args.timeout, port,
                            arch, smp=args.smp, bench_kind=args.bench_kind)

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
        else:
            try:
                proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                      timeout=args.timeout, check=False)
                log = proc.stdout + proc.stderr
            except subprocess.TimeoutExpired as exc:
                log = decode(exc.stdout) + decode(exc.stderr)
            metrics = {}

    parsed = parse_run_log(log, args.mode, metrics)
    passed = parsed["passed"]
    status = parsed["status"]
    metrics = parsed["metrics"]
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-vk-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
