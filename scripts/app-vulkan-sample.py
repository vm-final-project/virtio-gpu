#!/usr/bin/env python3
"""Boot the Vulkan sample with a VirtIO-GPU device and record the probe."""
from __future__ import annotations

import argparse
import pathlib
import subprocess

from common import acceleration, decode, default_qemu_binary, image_suffix, machine_and_cpu_args, normalize_arch, parse_vogue_timing, resolve_qemu, result, result_path, write_json

ROOT = pathlib.Path(__file__).resolve().parents[1]


def output_path(mode: str) -> pathlib.Path:
    return ROOT / "results" / "venus" / f"qemu_{mode}_probe.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--mode", choices=("2d", "venus-ring"), required=True)
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, required=True)
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)

    out = result_path(ROOT / "results/venus", f"qemu_{args.mode}_probe.json", arch)
    qemu = resolve_qemu(qemu_name)
    if not qemu:
        message = f"QEMU executable not found: {qemu_name}"
        write_json(out, result("blocked:qemu-missing", error=message))
        return 0

    image = ROOT / "build" / f"app-vulkan-sample_{image_suffix(arch)}"
    if not image.is_file():
        message = f"missing image: {image}"
        write_json(out, result("blocked:image-missing", error=message))
        return 0

    accel = acceleration(arch)
    device = "virtio-gpu-pci" if args.mode == "2d" else "virtio-gpu-gl-pci,blob=true,venus=true"
    command = [
        qemu, *machine_and_cpu_args(arch, accel), "-m", "512M",
        "-display", "none", "-serial", "stdio", "-device", device,
        "-kernel", str(image),
    ]
    try:
        run = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired as exc:
        text = decode(exc.stdout)
        write_json(out, result("blocked:timeout", command,
                               inputs={"mode": args.mode},
                               metrics={"output_tail": text[-2000:],
                                        "timing": parse_vogue_timing(text)},
                               error="QEMU probe timed out"))
        return 0

    text = decode(run.stdout)
    passed = run.returncode == 0 and (
        args.mode == "2d" or "venus_ring_protocol=pass" in text
    )
    status = "pass" if passed else "fail"
    write_json(out, result(status, command,
                           inputs={"mode": args.mode},
                           metrics={"output_tail": text[-2000:],
                                    "timing": parse_vogue_timing(text)},
                           error=None if passed else "probe did not report success"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
