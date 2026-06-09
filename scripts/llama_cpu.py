#!/usr/bin/env python3
"""Run the CPU llama.cpp bench or server appliance under QEMU."""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from common import acceleration, decode, resolve_model, resolve_qemu, result, write_json

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/llama"


def image(mode: str) -> Path:
    name = "vogue-llama-cpu" + ("-server" if mode == "server" else "")
    return ROOT / ".unikraft/build" / f"{name}_qemu-x86_64"


def qemu_command(qemu: str, model: Path, mode: str, timeout: int) -> list[str]:
    del model, timeout
    accel, cpu = acceleration()
    return [
        qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", "4096",
        "-nographic", "-no-reboot", "-kernel", str(image(mode)),
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    output = RESULTS / f"llama_{'server_' if args.mode == 'server' else ''}cpu.json"
    model = resolve_model(args.model)
    qemu = resolve_qemu(args.qemu)
    base = qemu_command(qemu or args.qemu, args.model, args.mode, args.timeout)
    inputs = {"mode": args.mode, "model": str(args.model), "image": str(image(args.mode))}
    blocker = (
        ("blocked:qemu-missing", "QEMU executable not found") if not qemu else
        ("blocked:model-missing", "Model file not found") if not model else
        ("blocked:image-missing", "Unikraft image not found") if not image(args.mode).is_file() else
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
            "-append", "console=ttyS0",
        ]
        try:
            proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                  timeout=args.timeout, check=False)
            log = proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as exc:
            log = decode(exc.stdout) + decode(exc.stderr)

    metrics: dict = {}
    if args.mode == "bench":
        match = re.search(r"uk-llama-cpu: pp512=([0-9.]+) tg128=([0-9.]+)", log)
        passed = bool(match and "uk-llama-cpu: PASS" in log)
        if match:
            metrics = {"pp512": float(match.group(1)), "tg128": float(match.group(2))}
    else:
        match = re.search(r"uk-llama-cpu-server: READY ([^\n]+)", log)
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
