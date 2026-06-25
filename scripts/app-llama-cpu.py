#!/usr/bin/env python3
"""Run the CPU llama.cpp bench or server appliance under QEMU."""
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


def image(mode: str, arch: str, bench_kind: str = "hand_rolled_smoke") -> Path:
    if mode == "bench" and bench_kind == "upstream_llama_bench":
        name = "vogue-llama-cpu-upstream-bench"
    else:
        name = "vogue-llama-cpu" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_{image_suffix(arch)}"


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, arch: str,
                 smp: int = 1, port: int = 0,
                 bench_kind: str = "hand_rolled_smoke") -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    command = [qemu, *machine_and_cpu_args(arch, accel), *smp_args(smp), "-m", "4096", "-nographic", "-no-reboot", "-kernel", str(image(mode, arch, bench_kind))]
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
            with urllib.request.urlopen(health, timeout=2) as response:
                body = response.read().decode("utf-8", "replace")
            health_body = json.loads(body)
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/completion",
                data=json.dumps({"prompt": "Hello", "n_predict": 8}).encode(),
                headers={"Content-Type": "application/json"},
            )
            started = time.monotonic()
            with urllib.request.urlopen(request, timeout=max(deadline - time.monotonic(), 1)) as response:
                completion = json.loads(response.read().decode("utf-8", "replace"))
            elapsed = max(time.monotonic() - started, 0.0001)
            content = completion.get("content", "")
            token_count = completion.get("tokens_predicted")
            if not isinstance(token_count, int) or token_count <= 0:
                token_count = len(content.split()) if content else 0
            return {
                "http_status": 200,
                "health": health_body,
                "completion_status": response.status,
                "server_toks_per_s": round(token_count / elapsed, 3) if token_count else 0.0,
                "requests_per_s": round(1 / elapsed, 3),
            }
        except Exception:
            time.sleep(0.5)
    return {}


def parse_run_log(log: str, mode: str, smp: int, http_metrics: dict) -> dict:
    placements = [
        {"target": int(target), "actual": int(actual)}
        for target, actual in re.findall(
            r"ggml-unikraft-place: target=(\d+) actual=(-?\d+)",
            log,
        )
    ]
    kernel_placements = [
        {"actual": int(actual), "target": int(target)}
        for actual, target in re.findall(
            r"SMPPLACE dequeue thread=.* on-lcpu=(\d+) sched-lcpu=(\d+) ready=1",
            log,
        )
    ]
    metrics = dict(http_metrics)
    ggml_actuals = sorted({p["actual"] for p in placements if p["actual"] >= 0})
    kernel_actuals = sorted({p["actual"] for p in kernel_placements if p["actual"] >= 0})
    expected = list(range(max(smp, 0)))
    actual_union = sorted(set(ggml_actuals) | set(kernel_actuals))

    if placements:
        metrics["placements"] = placements
    if kernel_placements:
        metrics["kernel_placements"] = kernel_placements
    metrics["placement_actuals_seen"] = actual_union
    metrics["placement_expected"] = expected
    metrics["placement_complete"] = bool(expected) and set(expected).issubset(actual_union)

    worker = re.search(
        r"uk-llama-upstream: workers=(\d+) cpu_mask_bits=(\d+) strict_cpu=1 poll=100",
        log,
    )
    if worker:
        metrics["ggml_workers"] = int(worker.group(1))
        metrics["ggml_cpu_mask_bits"] = int(worker.group(2))

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
        metrics["backend"] = "cpu"
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

    if mode == "bench":
        match = re.search(r"uk-llama-upstream: pp512=([0-9.]+) tg128=([0-9.]+)", log)
        if match:
            metrics["pp512"] = float(match.group(1))
            metrics["tg128"] = float(match.group(2))
        passed = bool(
            ("uk-llama-upstream: PASS" in log)
            and (
                (match is not None)
                or (metrics.get("bench_kind") == "upstream_llama_bench"
                    and "pp512" in metrics and "tg128" in metrics)
            )
        )
    else:
        match = re.search(r"uk-llama-upstream-server: READY ([^\n]+)", log)
        passed = bool(
            match
            and metrics.get("http_status") == 200
            and metrics.get("completion_status") == 200
        )
        if match:
            metrics["ready"] = match.group(0).strip()

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
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
    parser.add_argument("--bench-kind", choices=("hand_rolled_smoke", "upstream_llama_bench"),
                        default="hand_rolled_smoke")
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    bench_prefix = "upstream_bench_" if args.mode == "bench" and args.bench_kind == "upstream_llama_bench" else ""
    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else bench_prefix}cpu.json", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    port = 0
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, arch,
                        smp=args.smp, port=port, bench_kind=args.bench_kind)
    inputs = {"mode": args.mode, "arch": arch, "model": str(args.model),
              "image": str(image(args.mode, arch, args.bench_kind)), "smp": args.smp,
              "bench_kind": args.bench_kind, "backend": "cpu"}
    blocker = (
        ("blocked:qemu-missing", "QEMU executable not found") if not qemu else
        ("blocked:model-missing", "Model file not found") if not model else
        ("blocked:image-missing", "Unikraft image not found") if not image(args.mode, arch, args.bench_kind).is_file() else
        None
    )
    if blocker:
        write_json(output, result(blocker[0], base, inputs=inputs, error=blocker[1]))
        print(f"llama-cpu-{args.mode}: {blocker[0]}")
        return 0
    if args.mode == "server":
        port = free_port()
        base = qemu_command(qemu, args.model, args.mode, args.timeout, arch,
                            smp=args.smp, port=port, bench_kind=args.bench_kind)

    with tempfile.TemporaryDirectory(prefix="vogue-model-") as directory:
        shutil.copy(model, Path(directory) / "model.gguf")
        command = [
            *base,
            "-fsdev", f"local,id=model,path={directory},security_model=none",
            "-device", "virtio-9p-pci,fsdev=model,mount_tag=model",
            "-append", f"console={default_console(arch)}",
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
                proc = subprocess.run(command, cwd=ROOT, capture_output=True,
                                      timeout=args.timeout, check=False)
                log_bytes = (proc.stdout or b"") + (proc.stderr or b"")
                log = log_bytes.decode("utf-8", errors="replace")
            except subprocess.TimeoutExpired as exc:
                log = decode(exc.stdout) + decode(exc.stderr)
            metrics = {}

    parsed = parse_run_log(log, args.mode, args.smp, metrics)
    passed = parsed["passed"]
    status = parsed["status"]
    metrics = parsed["metrics"]
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-cpu-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
