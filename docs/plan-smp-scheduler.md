# A3 — Per-LCPU Cooperative Scheduler — SUPERSEDED

> **Status (2026-06-14): historical background only.** The per-LCPU scheduler
> bring-up described here is **done**. Active plan:
> **`docs/superpowers/plans/2026-06-14-multi-vcpu-speedup.md`**.

## Settled design (still true)

- `ukschedcoop` is per-instance self-contained (own run queue + idle thread), so
  running **one instance per online LCPU** is the design it already expects.
- A3 keeps `ukschedcoop`, instantiates one instance per online vCPU, makes the
  "current scheduler" per-CPU, and adds create-time/affinity placement —
  delivered as a Unikraft patch under `patches/unikraft/` plus VOGUE Kconfig.
- Implemented in `.deps/src/unikraft`: per-thread affinity state, ready
  scheduler registry (`uk_schedcoop_smp_sched/online_count/lcpu`),
  `uk_schedcoop_smp_place_current` self-migration, real `sched_{get,set}affinity`.

## Removed as stale

- `CONFIG_LIBPTHREAD_EMBEDDED`, global clone round-robin, and "scheduler-online
  logs prove ggml worker pinning" — all obsolete (musl-only build; placement is
  now explicit per-worker affinity).
- "General Linux-style pthread affinity + migration" is **out of scope**. The
  goal is the narrow one: place one llama worker per vCPU with minimal Unikraft
  changes.

## Remaining work

Runtime worker-count = online vCPU count, placement verification, and
measurement — see the active plan.
</content>
