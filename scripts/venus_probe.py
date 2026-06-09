#!/usr/bin/env python3
"""Boot the Vulkan sample with a VirtIO-GPU device and record the probe."""
from __future__ import annotations

import argparse
import pathlib
import subprocess

from common import acceleration, decode, resolve_qemu, result, write_json

ROOT = pathlib.Path(__file__).resolve().parents[1]


def output_path(mode: str) -> pathlib.Path:
    return ROOT / "results" / "venus" / f"qemu_{mode}_probe.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("2d", "venus-ring"), required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--timeout", type=int, required=True)
    args = parser.parse_args()

    out = output_path(args.mode)
    qemu = resolve_qemu(args.qemu)
    if not qemu:
        message = f"QEMU executable not found: {args.qemu}"
        write_json(out, result("blocked:qemu-missing", error=message))
        return 0

    image = ROOT / "build" / "app-vulkan-sample_qemu-x86_64"
    if not image.is_file():
        message = f"missing image: {image}"
        write_json(out, result("blocked:image-missing", error=message))
        return 0

    accel, cpu = acceleration()
    device = "virtio-gpu-pci" if args.mode == "2d" else "virtio-gpu-gl-pci,blob=true,venus=true"
    command = [
        qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", "512M",
        "-display", "none", "-serial", "stdio", "-device", device,
        "-kernel", str(image),
    ]
    try:
        run = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired as exc:
        text = decode(exc.stdout)
        write_json(out, result("blocked:timeout", command, {"mode": args.mode},
                               {"output_tail": text[-2000:]}, "QEMU probe timed out"))
        return 0

    text = decode(run.stdout)
    passed = run.returncode == 0 and (
        args.mode == "2d" or "venus_ring_protocol=pass" in text
    )
    status = "pass" if passed else "fail"
    write_json(out, result(status, command, {"mode": args.mode},
                           {"output_tail": text[-2000:]},
                           None if passed else "probe did not report success"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
