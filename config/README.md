# Config reference snapshots


This directory contains the tracked reference Unikraft `.config` snapshots used by
static evidence gates. Root-level `.config.*_qemu-x86_64` and
`.config.*_qemu-arm64` files are KraftKit build
outputs and are intentionally ignored.

The primary checked snapshot today is `config/.config.vogue_qemu-x86_64`, which
`make real-path-check` uses to ensure production builds select the real
`libukvirtio_gpu` backend rather than the fake native-test backend. The arm64
build path uses the same source selection rules; add a tracked arm64 snapshot
here if future gates need architecture-specific config assertions.
