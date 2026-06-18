#!/usr/bin/env python3
"""Run the CPU llama.cpp bench or server appliance under QEMU."""
from __future__ import annotations

import argparse
import re
import shutil
import tempfile
from pathlib import Path

from common import (
    acceleration,
    default_console,
    default_qemu_binary,
    file_size,
    free_port,
    image_suffix,
    machine_and_cpu_args,
    normalize_arch,
    probe_http_server,
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
    name = "vogue-llama-cpu" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_{image_suffix(arch)}"


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, port: int, arch: str) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    # -smp 1: the VOGUE unikernel is single-core only (the fixed comparison point
    # for the vogue-baselines 1-core variants). -m 8192 matches the guest RAM
    # budget given to the Linux/microvm baselines (QEMU does not pre-allocate, so
    # the ceiling does not inflate peak RSS).
    command = [qemu, *machine_and_cpu_args(arch, accel), "-smp", "1", "-m", "8192", "-nographic", "-no-reboot", "-kernel", str(image(mode, arch))]
    if mode == "server":
        # QEMU user-mode networking forwards host *port* to the guest's :8080
        # HTTP listener (DHCP hands the guest 10.0.2.15); same wiring as the VK
        # server appliance. Bench mode never serves, so it stays network-free.
        command += [
            "-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{port}-10.0.2.15:8080",
            "-device", "virtio-net-pci,netdev=net0",
        ]
    return command


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--query", default="hi, what's your name",
                        help="server mode: prompt sent to /completion and recorded with its reply")
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else ''}cpu.json", arch)
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
        print(f"llama-cpu-{args.mode}: {blocker[0]}")
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
            "-append", f"console={default_console(arch)}",
        ]
        if args.mode == "server":
            # Boot the appliance, wait for the model-ready /health, then send one
            # real query and record it alongside the model's completion.
            metrics, log, passed = probe_http_server(
                command, port=port,
                ready_marker="uk-llama-upstream-server: READY",
                query=args.query, timeout=args.timeout, cwd=ROOT)
        else:
            # Stream serial output so the boot marker can be timed. Bench mode
            # runs upstream llama-bench (same as the VK appliance), which loads
            # the model itself, so we time its "config" line: printed after boot
            # + 9p mount, just before llama-bench, giving launch->ready
            # boot_time_s separate from the pp512/tg128 inference numbers.
            run = run_timed(command, cwd=ROOT, timeout=args.timeout,
                            markers={"boot": "uk-llama-upstream: config"})
            log = run.text
            # The bench appliance runs upstream llama-bench, which prints a
            # markdown table; pull pp512/tg128 t/s from the test-column rows
            # (e.g. "| ... | pp512 | 17.71 ± 0.42 |"). Same shape app-llama-vk.py
            # and the vogue-baselines runners parse.
            pp = re.search(r"\|\s*pp512\s*\|\s*([0-9.]+)", log)
            tg = re.search(r"\|\s*tg128\s*\|\s*([0-9.]+)", log)
            passed = bool(pp and tg and "uk-llama-upstream: PASS" in log)
            metrics = {"boot_time_s": run.elapsed("boot"),
                       "peak_rss_kb": run.peak_rss_kb}
            if pp:
                metrics["pp512"] = float(pp.group(1))
            if tg:
                metrics["tg128"] = float(tg.group(1))

    # Footprint metrics common to both modes: bootable image size and the
    # appliance's own timed 9pfs weight load.
    metrics["image_bytes"] = file_size(image(args.mode, arch))
    load = re.search(r"model_load .*elapsed_ms=([0-9.]+)", log)
    metrics["model_load_ms"] = float(load.group(1)) if load else None

    status = "pass" if passed else "blocked:no-pass-marker"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-cpu-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
