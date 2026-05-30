#!/usr/bin/env python3
"""Reject stale/overclaiming VOGUE paper language.

This is intentionally lightweight: it is a guardrail for the current stage,
not a replacement for peer review. The check enforces terms that distinguish
software-render, real-driver readiness, and blocked acceleration claims.
"""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PAPER = ROOT / "paper"
FILES = sorted((PAPER / "sections").glob("*.typ")) + sorted((PAPER / "figures").glob("*.typ"))
TEXT = "\n".join(p.read_text(encoding="utf-8") for p in FILES)

FORBIDDEN = {
    "does not currently target Venus": "stale: VOGUE now targets the Venus transport substrate, not full Vulkan execution",
    "virgl context creation path (context creation and 3D command submission) is stubbed": "stale: real controlq commands are implemented",
    "virgl context creation stubs": "stale: replace stub language with explicit readiness/blocker language",
    "Blob support is the natural next capability spike": "stale: blob controlq support now exists as readiness evidence",
    "Resource blobs / host-visible mem], [Zero-copy buffer sharing], [Not yet implemented": "stale appendix status",
    "VOGUE does not currently target Venus": "stale: use transport-substrate wording",
    "The real virgl GPU device probe (xport.gl-probe) succeeds": "overclaim for current stage; xport.qemu-vgpu is blocked",
}

REQUIRED = {
    "blocked:modern-pci-unsupported": "resolved historical QEMU/Venus transport blocker must remain visible",
    "0x1050": "modern VirtIO-GPU PCI ID blocker must be named",
    "proto.real-driver": "real-driver ABI/readiness row must be visible",
    "xport.qemu-vgpu": "QEMU/Venus transport row must be visible",
    "vk.readiness": "benchmark/evaluation-design row must be visible",
    "make stage-check": "artifact instructions must expose stage check",
    "make benchmark-check": "artifact instructions must expose benchmark check",
    "make venus-check": "artifact instructions must expose Venus/readiness check",
    "Unikraft unikernel": "paper must remain framed as a Unikraft unikernel design",
    "Unikraft": "paper must remain framed as a Unikraft design",
}

errors: list[str] = []
for phrase, reason in FORBIDDEN.items():
    if phrase in TEXT:
        errors.append(f"forbidden phrase present: {phrase!r} ({reason})")
for phrase, reason in REQUIRED.items():
    if phrase not in TEXT:
        errors.append(f"required phrase missing: {phrase!r} ({reason})")

# STK porting is out of scope (plan.md §0.5); no STK-related language is expected in the paper.

if errors:
    print("paper_consistency_check: FAIL", file=sys.stderr)
    for e in errors:
        print(f"- {e}", file=sys.stderr)
    sys.exit(1)

print(f"paper_consistency_check: PASS files={len(FILES)} required={len(REQUIRED)} forbidden={len(FORBIDDEN)}")
