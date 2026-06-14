#!/usr/bin/env python3
"""Run the pthread affinity probe appliance under QEMU."""
from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path

from common import (
    acceleration,
    decode,
    default_console,
    default_qemu_binary,
    image_suffix,
    machine_and_cpu_args,
    normalize_arch,
    resolve_qemu,
    result,
    result_path,
    smp_args,
    write_json,
)

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/smp"
WORKER_RE = re.compile(
    r"^pthread-affinity: worker=(?P<worker>\d+) "
    r"requested=(?P<requested>0x[0-9a-fA-F]+) "
    r"actual=(?P<actual>\d+) samples=(?P<samples>\d+)$",
    re.MULTILINE,
)
PASS_RE = re.compile(
    r"^pthread-affinity: PASS workers=(?P<workers>\d+) distinct=(?P<distinct>\d+)$",
    re.MULTILINE,
)
FAIL_RE = re.compile(r"^pthread-affinity: FAIL reason=(?P<reason>[a-z0-9-]+)$", re.MULTILINE)


def image(arch: str) -> Path:
    return ROOT / ".unikraft/build" / f"vogue-pthread-affinity_{image_suffix(arch)}"


def qemu_command(qemu: str, timeout: int, arch: str, smp: int = 1, rounds: int | None = None) -> list[str]:
    del timeout
    accel = acceleration(arch)
    append = [f"console={default_console(arch)}"]
    if rounds is not None:
        append.append(f"pthread_affinity_rounds={rounds}")
    return [
        qemu,
        *machine_and_cpu_args(arch, accel),
        *smp_args(smp),
        "-m",
        "512",
        "-nographic",
        "-no-reboot",
        "-kernel",
        str(image(arch)),
        "-append",
        " ".join(append),
    ]


def parse_probe_log(log: str) -> dict:
    failed = FAIL_RE.search(log)
    if failed:
        raise ValueError(f"probe failed: {failed.group('reason')}")

    placements = [
        {
            "worker": int(match.group("worker")),
            "requested": match.group("requested").lower(),
            "actual": int(match.group("actual")),
            "samples": int(match.group("samples")),
        }
        for match in WORKER_RE.finditer(log)
    ]
    passed = PASS_RE.search(log)
    if not placements or not passed:
        raise ValueError("probe log missing PASS markers")

    return {
        "status": "pass",
        "metrics": {
            "workers": int(passed.group("workers")),
            "distinct": int(passed.group("distinct")),
            "placements": placements,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", default="x86_64")
    parser.add_argument("--qemu")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--smp", type=int, default=int(os.environ.get("VOGUE_SMP", "1")))
    args = parser.parse_args()
    arch = normalize_arch(args.arch)
    qemu_name = args.qemu or default_qemu_binary(arch)
    qemu = resolve_qemu(qemu_name)
    rounds = os.environ.get("PTHREAD_AFFINITY_ROUNDS")
    rounds_value = int(rounds) if rounds else None

    output = result_path(RESULTS, "pthread_affinity.json", arch)
    command = qemu_command(qemu or qemu_name, args.timeout, arch, smp=args.smp, rounds=rounds_value)
    inputs = {"arch": arch, "image": str(image(arch)), "smp": args.smp}
    if rounds_value is not None:
        inputs["rounds"] = rounds_value

    blocker = (
        ("blocked:qemu-missing", "QEMU executable not found") if not qemu else
        ("blocked:image-missing", "Unikraft image not found") if not image(arch).is_file() else
        None
    )
    if blocker:
        write_json(output, result(blocker[0], command, inputs=inputs, error=blocker[1]))
        print(f"pthread-affinity: {blocker[0]}")
        return 0

    try:
        proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                              timeout=args.timeout, check=False)
        log = proc.stdout + proc.stderr
    except subprocess.TimeoutExpired as exc:
        log = decode(exc.stdout) + decode(exc.stderr)
        write_json(output, result("blocked:timeout", command, inputs=inputs, error=log[-2000:]))
        print("pthread-affinity: blocked:timeout")
        return 0

    try:
        parsed = parse_probe_log(log)
    except ValueError as exc:
        write_json(
            output,
            result("blocked:probe-failed", command, inputs=inputs, error=f"{exc}\n{log[-2000:]}"),
        )
        print("pthread-affinity: blocked:probe-failed")
        return 0

    write_json(output, result(parsed["status"], command, inputs=inputs, metrics=parsed["metrics"]))
    print("pthread-affinity: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
