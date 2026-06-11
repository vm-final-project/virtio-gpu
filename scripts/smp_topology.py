"""Thread -> LCPU placement policy for the A3 per-LCPU scheduler.

Mirrors the C rule in patches/unikraft/0002-smp-per-lcpu-coop-scheduler.patch.
Round-robin so each successive thread lands on the next vCPU; with one ggml
worker per vCPU this pins exactly one worker per LCPU, which makes ggml's spin
barrier correct (see docs/plan-smp-vcpu.md Phase 3.0).
"""
from __future__ import annotations


def place(thread_seq: int, lcpu_count: int) -> int:
    """Return the LCPU index a thread with sequence number `thread_seq` pins to."""
    if lcpu_count <= 1:
        return 0
    return thread_seq % lcpu_count
