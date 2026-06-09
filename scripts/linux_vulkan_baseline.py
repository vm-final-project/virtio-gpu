#!/usr/bin/env python3
"""Run the stock-Linux QEMU Vulkan baseline."""
from __future__ import annotations

import argparse
import pathlib
import subprocess

from common import acceleration, decode, resolve_model, resolve_qemu, result, write_json

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "llama" / "vulkan_qemu_linux_baseline.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--timeout", type=int, required=True)
    parser.add_argument("--kernel", default="/boot/vmlinuz")
    parser.add_argument("--initramfs", default="build/linux-vulkan-initramfs.cpio.gz")
    args = parser.parse_args()

    try:
        model = resolve_model(args.model)
        qemu = resolve_qemu(args.qemu)
    except FileNotFoundError as exc:
        write_json(OUT, result("blocked:input-missing", error=str(exc)))
        return 0

    kernel = pathlib.Path(args.kernel)
    initramfs = pathlib.Path(args.initramfs)
    if not kernel.is_file() or not initramfs.is_file():
        message = f"missing Linux guest input: kernel={kernel}, initramfs={initramfs}"
        write_json(OUT, result("blocked:guest-image-missing",
                               inputs={"kernel": str(kernel), "initramfs": str(initramfs)},
                               error=message))
        return 0

    accel, cpu = acceleration()
    command = [
        qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", "4G",
        "-display", "none", "-serial", "stdio",
        "-device", "virtio-gpu-gl-pci,blob=true,venus=true",
        "-kernel", str(kernel), "-initrd", str(initramfs),
        "-append", f"console=ttyS0 vogue.model={model}",
    ]
    try:
        run = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired as exc:
        text = decode(exc.stdout)
        write_json(OUT, result("blocked:timeout", command, {"model": str(model)},
                               {"output_tail": text[-2000:]}, "baseline timed out"))
        return 0

    text = decode(run.stdout)
    passed = run.returncode == 0 and "VK-LINUX-GUEST-PASS" in text
    write_json(OUT, result("pass" if passed else "fail", command,
                           {"model": str(model)}, {"output_tail": text[-2000:]},
                           None if passed else "Linux guest baseline failed"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
