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
    image_suffix,
    machine_and_cpu_args,
    normalize_arch,
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


def qemu_command(qemu: str, model: Path, mode: str, timeout: int, arch: str) -> list[str]:
    del model, timeout
    accel = acceleration(arch)
    return [qemu, *machine_and_cpu_args(arch, accel), "-m", "4096", "-nographic", "-no-reboot", "-kernel", str(image(mode, arch))]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("bench", "server"), required=True)
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    output = result_path(RESULTS, f"llama_{'server_' if args.mode == 'server' else ''}cpu.json", arch)
    model = resolve_model(args.model)
    qemu = resolve_qemu(qemu_name)
    base = qemu_command(qemu or qemu_name, args.model, args.mode, args.timeout, arch)
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

    with tempfile.TemporaryDirectory(prefix="vogue-model-") as directory:
        shutil.copy(model, Path(directory) / "model.gguf")
        command = [
            *base,
            "-fsdev", f"local,id=model,path={directory},security_model=none",
            "-device", "virtio-9p-pci,fsdev=model,mount_tag=model",
            "-append", f"console={default_console(arch)}",
        ]
        # Stream serial output so the unikernel's "booted" marker can be timed.
        # It is printed at app entry, before the 9pfs mount and the multi-second
        # weight load, so boot_time_s reflects pure unikernel startup (not model
        # load, which the appliance reports separately on its model_load line).
        # The tag differs per mode. (The server stays up, so run_timed returns
        # at the timeout; the marker is still captured from the streamed log.)
        boot_marker = ("uk-llama-upstream: booted" if args.mode == "bench"
                       else "uk-llama-upstream-server: booted")
        run = run_timed(command, cwd=ROOT, timeout=args.timeout,
                        markers={"boot": boot_marker})
        log = run.text

    # Footprint metrics: model_load_ms (the appliance times its own 9pfs weight
    # load), peak host RSS of the QEMU process, and the bootable image size.
    load = re.search(r"model_load .*elapsed_ms=([0-9.]+)", log)
    metrics: dict = {
        "boot_time_s": run.elapsed("boot"),
        "model_load_ms": float(load.group(1)) if load else None,
        "peak_rss_kb": run.peak_rss_kb,
        "image_bytes": file_size(image(args.mode, arch)),
    }
    if args.mode == "bench":
        match = re.search(r"uk-llama-upstream: pp512=([0-9.]+) tg128=([0-9.]+)", log)
        passed = bool(match and "uk-llama-upstream: PASS" in log)
        if match:
            metrics["pp512"] = float(match.group(1))
            metrics["tg128"] = float(match.group(2))
    else:
        match = re.search(r"uk-llama-upstream-server: READY ([^\n]+)", log)
        passed = bool(match)
        if match:
            metrics["ready"] = match.group(0).strip()

    status = "pass" if passed else "blocked:no-pass-marker"
    write_json(output, result(status, command, inputs=inputs, metrics=metrics,
                              error=None if passed else log[-2000:]))
    print(f"llama-cpu-{args.mode}: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
