# Multi-vCPU (SMP) Support for the llama.cpp Appliances — SUPERSEDED

> **Status (2026-06-14): historical.** This document's Phase 0–3 plan is done or
> obsolete. Active plan: **`docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`**.

## What is settled (do not re-plan)

- **SMP bring-up works.** `-smp N` under KVM boots N vCPUs; one `ukschedcoop`
  instance per LCPU comes online (`SMPPLACE online lcpu=0..N-1`). Fixes: low
  `.bss` AP stack, AP adopts runtime CR3 after EFER.NXE, `uk_lcpu_init()` in AP
  entry, SMP-safe bbuddy spinlock, per-instance run-queue spinlock.
- **Harness wiring done.** `smp_args()` + `--smp`/`VOGUE_SMP` in
  `scripts/common.py` and the llama runners.
- **Per-LCPU placement + affinity implemented in Unikraft** (`uksched`
  `sched_{get,set}affinity`, `uk_schedcoop_smp_place_current`); ggml `ggml-cpu.c`
  has a Unikraft affinity path that pins one worker per vCPU.

## What was wrong in the original framing (removed)

- The old "no SMP scheduler / pthread-embedded bypasses clone" blocker is
  obsolete: the build is musl-only, clone reaches `uk_clone`, and per-LCPU
  schedulers exist.

## The remaining problem (now the active plan)

Worker count is fixed at build time (`@@VOGUE_SMP@@`) and mismatches launch-time
`--smp`; polling workers > online vCPUs **deadlock** the cooperative scheduler.
Fix = make `n_threads`/cpumask track the *online* vCPU count at runtime, verify
one-worker-per-vCPU placement, then measure (`pp512` is the expected win;
`tg128` is bandwidth-bound). See `docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`.
</content>
