#!/usr/bin/env python3
"""Run the CPU llama.cpp bench or server appliance under QEMU."""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import tempfile
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


def image(mode: str, arch: str) -> Path:
    name = "vogue-llama-cpu" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_{image_suffix(arch)}"


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, arch: str, smp: int = 1) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    return [qemu, *machine_and_cpu_args(arch, accel), *smp_args(smp), "-m", "4096", "-nographic", "-no-reboot", "-kernel", str(image(mode, arch))]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else ''}cpu.json", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, arch, smp=args.smp)
    inputs = {"mode": args.mode, "arch": arch, "model": str(args.model), "image": str(image(args.mode, arch)), "smp": args.smp}
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

    with tempfile.TemporaryDirectory(prefix="vogue-model-") as directory:
        shutil.copy(model, Path(directory) / "model.gguf")
        command = [
            *base,
            "-fsdev", f"local,id=model,path={directory},security_model=none",
            "-device", "virtio-9p-pci,fsdev=model,mount_tag=model",
            "-append", f"console={default_console(arch)}",
        ]
        try:
            proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                  timeout=args.timeout, check=False)
            log = proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as exc:
            log = decode(exc.stdout) + decode(exc.stderr)

    metrics: dict = {}
    if args.mode == "bench":
        match = re.search(r"uk-llama-upstream: pp512=([0-9.]+) tg128=([0-9.]+)", log)
        passed = bool(match and "uk-llama-upstream: PASS" in log)
        if match:
            metrics = {"pp512": float(match.group(1)), "tg128": float(match.group(2))}
    else:
        match = re.search(r"uk-llama-upstream-server: READY ([^\n]+)", log)
        passed = bool(match)
        if match:
            metrics = {"ready": match.group(0).strip()}

    status = "pass" if passed else "blocked:no-pass-marker"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-cpu-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
