# `tests/` - VOGUE host-native test suite

Deterministic C tests for reusable VirtIO-GPU and Venus pure logic. These tests
compile selected project sources directly and link one self-checking binary per
test. They do not instantiate a fake VirtIO-GPU device; QEMU/PCIe device
behavior is covered by the runtime gates.

llama.cpp runtime coverage lives in the upstream single-application appliances;
this suite only proves protocol structs and command encoders.

## Package Layout

```text
tests/
  Makefile                         Target groups and compiler rules
  README.md                        This file
  test_utils.h                     Test assertions
  shim/uk/                         Host shims for selected Unikraft headers

libs/libukvirtio_gpu/tests/
  proto_abi.c                      VirtIO-GPU wire ABI validation

libs/libukvulkan_venus/tests/
  capset.c                         Venus capset decoder checks
  encoder.c                        Venus protocol encoder checks
```

## Quick Start

Run from the repository root:

```sh
make native-tests       # full host-native suite
make test-venus         # Venus encoder and capset checks
make proto-abi          # VirtIO-GPU wire-ABI struct/feature checks
```

`make test-fast` bundles `native-tests` and `proto-abi`.

The same targets exist on the component Makefile:

```sh
make -C tests native
make -C tests test-venus
make -C tests proto-abi
```

## Test Groups

| Group | Target | Binaries |
|---|---|---|
| Venus | `test-venus` | `venus_encoder`, `venus_capset` |
| Conditional | `proto-abi` | `proto_abi` |

## Running A Single Test

```sh
make -C tests venus-encoder
make -C tests venus-capset
make -C tests proto-abi
```

## Vulkan Headers

The Venus tests compile generated Venus code that references
`VK_HEADER_VERSION 352` types. `VK_INC` defaults to the repo-pinned
`.deps/src/Vulkan-Headers/include` via `VULKAN_HEADERS_INCLUDE` when invoked
from the root Makefile. Override with:

```sh
make -C tests <target> VK_INC=/path/to/Vulkan-Headers/include
```

## Expected PASS Output

```text
venus_encoder: PASS checks=25
venus_capset: PASS checks=11
proto_abi passed ctrl_hdr=24 display_info=408 edid=1056
```

## How It Works

* `native` compiles all retained binaries in parallel, then runs them serially
  for stable output ordering.
* Library sources under test are pure encoders/decoders:
  `protocol/venus_cs.c`, `protocol/venus_compute.c`, and `ring/venus_ring.c` capset decode helpers.
* `shim/uk/*.h` provides host-compilable stand-ins for selected Unikraft
  headers used by those sources.

Runtime evidence that requires an actual VirtIO-GPU device belongs in
`venus-check`, `vulkan-check`, and the llama appliance gates.
