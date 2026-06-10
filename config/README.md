# Config reference snapshots

Tracked Unikraft `.config` snapshots used by static evidence gates. Root-level
`.config.*_qemu-{x86_64,arm64}` files are KraftKit build outputs and are
gitignored — only snapshots committed under this directory are authoritative.

The primary one is `config/.config.vogue_qemu-x86_64`, which `make
real-path-check` uses to confirm production builds select the real
`libukvirtio_gpu` backend rather than the fake native-test backend. The arm64
build uses the same source-selection rules; add a tracked arm64 snapshot here if
a future gate needs arch-specific config assertions.
