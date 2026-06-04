#!/usr/bin/env python3
"""Capture real build evidence for bld.host.vk.

Inspects the actual Unikraft VK appliance image produced by
`make llama-upstream-vk-build` and records whether the upstream ggml Vulkan
backend genuinely compiled and linked: the image must exist and expose
ggml_vk_*/ggml_backend_vk_* symbols (proving ggml-vulkan.cpp + the Venus/ICD
libraries are linked in), and the build must have used -DGGML_USE_VULKAN=1.

Writes results/llama/vulkan_build.json (read by eval_matrix bld.host.vk).
Honest: status=pass only when the symbols are actually present in the image.
"""
from __future__ import annotations

import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results" / "llama"
IMAGE = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-vk_qemu-x86_64"
DBG = Path(str(IMAGE) + ".dbg")
MAKEFILE_UK = ROOT / "apps" / "app-llama-upstream-vk" / "Makefile.uk"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "vulkan_build.json").write_text(json.dumps(payload, indent=2) + "\n")


def _vk_symbol_count(path: Path) -> int:
    try:
        out = subprocess.run(["nm", str(path)], text=True, capture_output=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return 0
    return sum(1 for ln in out.splitlines() if "ggml_vk_" in ln or "ggml_backend_vk" in ln)


def main() -> int:
    img = IMAGE if IMAGE.exists() else None
    if img is None:
        _write({
            "schema": "llama/vulkan-build.v2", "evidence_id": "bld-host-vk",
            "status": "blocked:llama-vulkan-appliance-not-built", "pass": False,
            "generated_utc": _now(),
            "claim_allowed": "Build blocker documented; no link/compile claim.",
            "claim_forbidden": "Vulkan execution or token claim.",
            "next_step": "run: make llama-upstream-vk-build",
        })
        print("vk-build-capture: blocked:llama-vulkan-appliance-not-built")
        return 0

    nm_target = DBG if DBG.exists() else IMAGE
    syms = _vk_symbol_count(nm_target)
    uses_vulkan = "GGML_USE_VULKAN=1" in MAKEFILE_UK.read_text() if MAKEFILE_UK.exists() else False
    ok = syms > 0 and uses_vulkan
    _write({
        "schema": "llama/vulkan-build.v2", "evidence_id": "bld-host-vk",
        "status": "pass" if ok else "blocked:vk-backend-not-linked", "pass": ok,
        "generated_utc": _now(),
        "image": str(IMAGE.relative_to(ROOT)),
        "image_bytes": IMAGE.stat().st_size,
        "ggml_vk_symbols": syms,
        "ggml_use_vulkan": uses_vulkan,
        "claim_allowed": (f"Upstream ggml Vulkan backend compiles inside the Unikraft clang toolchain and links "
                          f"into the appliance against in-tree ggml-vulkan/libvulkan/libukvulkan_venus: {syms} ggml_vk_* "
                          f"symbols present with -DGGML_USE_VULKAN=1."),
        "claim_forbidden": "Vulkan execution, llama tokens via GPU, or any throughput claim.",
        "next_step": "Boot under a venus-capable QEMU to advance llm.bench.vk.",
    })
    print(f"vk-build-capture: {'pass' if ok else 'blocked'} ggml_vk_symbols={syms} ggml_use_vulkan={uses_vulkan}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
