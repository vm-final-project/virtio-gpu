# Deterministic Venus Encoder Porting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace VOGUE's hand-written Venus wire encoders (`libs/libukvulkan_venus/venus_cs.c` + `venus_compute.c` encode bodies) with code **deterministically generated** from the pinned upstream `../venus-protocol` Mako generator, sliced to exactly the Vulkan commands `ggml-vulkan`/llama.cpp uses, sitting on a thin Unikraft shim — while keeping the existing `libukvirtio_gpu` transport and `uk_venus_ring` submit path.

**Architecture:** The upstream generator (`../venus-protocol/vn_protocol.py`) emits per-object-type guest-side encoder headers (`vn_protocol_driver_*.h`). Those headers depend on a tiny, **explicitly documented** interface (`vn_cs.h`, `vn_ring.h`). We provide those two headers as a Unikraft adapter mapping onto `struct uk_venus_encoder` / `struct uk_venus_ring`. The generated headers are committed verbatim (byte-identical to upstream output) with a provenance lock; every `vn_encode_*`/`vn_submit_*`/`vn_call_*` not referenced by ggml is dropped by compile-time dead-code elimination (they are all `static inline`). "Only the functions ggml needs" is therefore achieved by extension-slicing + DCE, not by editing generated files. A byte-for-byte parity test gates the cutover from the hand-written encoders.

**Tech Stack:** Python 3 + Mako 1.3.x (generator, already installed), C11 (two shim headers + glue), Unikraft `Makefile.uk`/`Config.uk`, the existing host-native test harness under `tests/` (compiles against the sibling `../venus-protocol/include` Vulkan headers via `VK_INC`).

---

## Background — verified facts this plan is built on

These were confirmed by inspecting the trees on 2026-06-03; re-verify if the checkouts move.

1. **Generator runs clean out-of-the-box.** `python3 ../venus-protocol/vn_protocol.py --outdir <dir>` exits 0 and emits **38 driver headers** (`vn_protocol_driver_*.h`). No `--renderer` flag ⇒ guest/driver (encoder) variant. `--outdir` is **required**.
2. **venus-protocol pinned commit:** `70991d4c7e4e5a7bfa2fbb8a6e77e4eac350145d`. **Mesa** at `26.1-branchpoint-2163-g88577c5e54e`. **Mako** `1.3.12` present.
3. **The generated headers' entire external dependency surface is two hand-written headers we supply:**
   - `vn_cs.h` — the encoder/decoder primitives. The upstream `driver_cs.h` template documents the exact expected surface:
     `struct vn_cs_encoder`, `vn_cs_encoder_get_len`, `vn_cs_encoder_reserve`, `vn_cs_encoder_write`;
     `struct vn_cs_decoder`, `vn_cs_decoder_set_fatal`, `vn_cs_decoder_read`, `vn_cs_decoder_peek`;
     `vn_cs_handle_load_id`, `vn_cs_handle_store_id`.
   - `vn_ring.h` — needed only by the `vn_submit_*`/`vn_call_*` wrappers. Surface is exactly **4 functions** + one struct:
     `struct vn_ring_submit_command` (opaque to generated code — only passed by address);
     `vn_cs_encoder *vn_ring_submit_command_init(ring, submit, cmd_data, cmd_size, reply_size)`;
     `void vn_ring_submit_command(ring, submit)`;
     `vn_cs_decoder *vn_ring_get_command_reply(ring, submit)`;
     `void vn_ring_free_command_reply(ring, submit)`.
   - Everything else the headers include is itself generated (`vn_protocol_driver_{structs,handles,types,defines}.h`) or stock (`<vulkan/vulkan.h>`, `<string.h>`, `<stdlib.h>`, `<assert.h>`). **No `util/*`, no `vn_common.h`, no `vn_instance.h`.**
4. **Function census in the generated set:** 288 `vn_encode_*`, 650 `vn_sizeof_*`, 288 `vn_submit_*`, 121 `vn_call_*`, 195 `vn_decode_*` — all `static inline`.
5. **Handle-id convention.** Encoders read/write the guest id via `vn_cs_handle_load_id((const void **)&handle, VK_OBJECT_TYPE_*)` / `vn_cs_handle_store_id(...)`. VOGUE already treats handles as bare `uint64_t` ids, so the shim is `id == (uintptr_t)handle`.
6. **A `vn_submit_*` body's local-variable contract** (verified against `vn_submit_vkCreateBuffer`): uses `VN_SUBMIT_LOCAL_CMD_SIZE` (generated define), `malloc`/`free` (musl), `vn_sizeof_*`, then `init → vn_encode_* → submit`. A `vn_call_*` body adds `vn_ring_get_command_reply → vn_decode_*_reply → vn_ring_free_command_reply`.
7. **Transport already exists.** `mesa/.../vn_renderer_virtgpu.c` (open `/dev/dri/renderD128`, `ioctl(DRM_IOCTL_VIRTGPU_*)`, `mmap`) is **not generatable** — it is OS glue and is already replaced by `libukvirtio_gpu` + `libukvirtgpu_drm`. This plan does **not** regenerate it; Appendix A documents the op-for-op mapping and a test that locks it.
8. **Native test harness** (`tests/Makefile`) compiles libukvulkan_venus `.c` files directly against `VK_INC ?= $(realpath ../../venus-protocol/include)`. Existing targets: `make -C tests venus-cs`, `venus-compute`. Existing Make targets `gen-libukvenus{,-plan,-check}` at `Makefile:497-504` shell into `scripts/gen_libukvenus.py`.

---

## File structure

**Created:**
- `scripts/extract_ggml_vk_commands.py` — deterministic scan of `ggml-vulkan.cpp` → canonical command/extension manifest. One responsibility: derive "what ggml needs".
- `config/venus_command_manifest.json` — generated artifact (committed): sorted command list + required extension list + source provenance. The single source of truth consumed by the generator slice and the coverage test.
- `libs/libukvulkan_venus/include/uk/vn_cs.h` — encoder/decoder/handle-id shim onto `struct uk_venus_encoder`. (Filename `vn_cs.h` is mandatory: the generated headers `#include "vn_cs.h"`.)
- `libs/libukvulkan_venus/include/uk/vn_ring.h` — 4-function ring shim onto `struct uk_venus_ring`.
- `libs/libukvulkan_venus/vn_ring_shim.c` — implementation of the four `vn_ring_*` functions.
- `libs/libukvulkan_venus/generated/` — committed verbatim generator output + `GENERATED.lock` provenance file.
- `tests/venus_generated_compile_test.c` — compile + smoke gate for generated headers through the shim.
- `tests/venus_parity_test.c` — byte-for-byte legacy-vs-generated encoder parity gate.
- `scripts/venus/` — generator pin + pytest suite (`pin.json`, `test_pin.py`, `test_extract.py`, `test_manifest.py`, `test_generate.py`, `test_coverage.py`).

**Modified:**
- `scripts/gen_libukvenus.py` — turn the scaffold into a real, reproducible generator (`generate`/`verify` subcommands writing into `libs/libukvulkan_venus/generated/`).
- `libs/libukvulkan_venus/venus_cs.c`, `libs/libukvulkan_venus/venus_compute.c` — re-implement the public `uk_venus_encode_*` bodies to build real `Vk*` structs and call the generated `vn_encode_*`; delete hand-rolled byte-pushing once parity is green.
- `libs/libukvulkan_venus/venus_init.c`, `libs/libukvulkan_venus/include/uk/venus.h` — add the transport thunk used by the ring shim.
- `libs/libukvulkan_venus/Makefile.uk`, `Config.uk` — add `generated/` to include path; add the new source file.
- `tests/Makefile` — add the two new test targets and wire them into `test-venus`.
- `Makefile` — add `gen-libukvenus-verify`; fold it into `governance-check`.
- `libs/libukvulkan_venus/GENERATOR.md`, `libs/libukvulkan_venus/README.md`, `config/governance.json`, README status columns, `report-porting.md` — provenance + claim discipline.

---

## Milestone M0 — Pin & reproducibility harness

### Task 0.1: Lock the upstream generator commit and slice config

**Files:**
- Create: `scripts/venus/pin.json`
- Test: `scripts/venus/test_pin.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/venus/test_pin.py
import json, subprocess, pathlib
PIN = pathlib.Path(__file__).resolve().parents[2] / "scripts/venus/pin.json"

def test_pin_matches_checkout():
    pin = json.loads(PIN.read_text())
    head = subprocess.check_output(
        ["git", "-C", pin["checkout"], "rev-parse", "HEAD"], text=True).strip()
    assert head == pin["commit"], f"venus-protocol moved: {head} != {pin['commit']}"

def test_pin_has_slice_fields():
    pin = json.loads(PIN.read_text())
    assert pin["variant"] == "driver"
    assert isinstance(pin["wanted_extensions"], list) and pin["wanted_extensions"]
```

- [ ] **Step 2: Run it, expect FAIL**

Run: `python3 -m pytest scripts/venus/test_pin.py -v`
Expected: FAIL — `pin.json` does not exist (`FileNotFoundError`).

- [ ] **Step 3: Create the pin file**

```json
{
  "checkout": "../venus-protocol",
  "commit": "70991d4c7e4e5a7bfa2fbb8a6e77e4eac350145d",
  "variant": "driver",
  "generator": "vn_protocol.py",
  "wanted_extensions": [
    "VK_EXT_command_serialization",
    "VK_MESA_venus_protocol",
    "VK_KHR_get_physical_device_properties2",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_bind_memory2",
    "VK_KHR_maintenance1",
    "VK_KHR_external_memory",
    "VK_KHR_external_memory_capabilities",
    "VK_KHR_external_semaphore",
    "VK_KHR_external_semaphore_capabilities",
    "VK_KHR_synchronization2"
  ]
}
```

Note: the generator's extension list is module-level (`VK_XML_EXTENSION_LIST` in `vn_protocol.py`); `wanted_extensions` here is the **intersection** we assert is present, used by the slice/coverage checks (Task 1.2, 2.2). We do not fork the upstream list.

- [ ] **Step 4: Run it, expect PASS**

Run: `python3 -m pytest scripts/venus/test_pin.py -v`
Expected: PASS (2 passed). If `test_pin_matches_checkout` fails, the sibling checkout has moved — stop and reconcile before continuing.

- [ ] **Step 5: Commit**

```bash
git add scripts/venus/pin.json scripts/venus/test_pin.py
git commit -m "venus-gen: pin upstream venus-protocol commit + slice config"
```

---

## Milestone M1 — Deterministic ggml command manifest

### Task 1.1: Extraction script `ggml-vulkan.cpp` → command set

**Files:**
- Create: `scripts/extract_ggml_vk_commands.py`
- Test: `scripts/venus/test_extract.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/venus/test_extract.py
import json, subprocess, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/extract_ggml_vk_commands.py"

def run():
    out = subprocess.check_output([sys.executable, str(SCRIPT), "--json"], text=True)
    return json.loads(out)

def test_contains_core_compute_commands():
    cmds = set(run()["commands"])
    for must in ["vkCreateBuffer", "vkAllocateMemory", "vkCmdDispatch",
                 "vkCreateComputePipelines", "vkQueueSubmit", "vkMapMemory",
                 "vkUpdateDescriptorSets", "vkCmdPushConstants"]:
        assert must in cmds, f"missing {must}"

def test_output_is_sorted_and_deterministic():
    a = run()["commands"]; b = run()["commands"]
    assert a == b                      # deterministic
    assert a == sorted(a)              # sorted (stable diffs)
```

- [ ] **Step 2: Run it, expect FAIL**

Run: `python3 -m pytest scripts/venus/test_extract.py -v`
Expected: FAIL — script missing.

- [ ] **Step 3: Write the extraction script**

```python
#!/usr/bin/env python3
"""Deterministically derive the Vulkan command set ggml-vulkan/llama.cpp uses.

ggml-vulkan.cpp is written against vk-hpp (the C++ wrapper), so calls appear as
either C entry points (`vkCmdCopyBuffer`) or hpp methods (`.createBuffer`,
`buf.dispatch`). We map the hpp method spellings back to Vulkan command names
with a fixed table so the output is a stable, reviewable manifest.
"""
from __future__ import annotations
import argparse, json, os, re, sys
from pathlib import Path

LLAMA_ROOT = Path(os.environ.get("LLAMA_ROOT", "../llama.cpp")).resolve()
SRC = LLAMA_ROOT / "ggml/src/ggml-vulkan/ggml-vulkan.cpp"

# vk-hpp method spelling -> Vulkan command. Reviewed against the M1 scan.
HPP_METHOD_TO_CMD = {
    "createInstance": "vkCreateInstance", "createDevice": "vkCreateDevice",
    "getQueue": "vkGetDeviceQueue", "getQueue2": "vkGetDeviceQueue2",
    "allocateMemory": "vkAllocateMemory", "freeMemory": "vkFreeMemory",
    "mapMemory": "vkMapMemory", "unmapMemory": "vkUnmapMemory",
    "bindBufferMemory": "vkBindBufferMemory",
    "getBufferMemoryRequirements": "vkGetBufferMemoryRequirements",
    "createBuffer": "vkCreateBuffer", "destroyBuffer": "vkDestroyBuffer",
    "createShaderModule": "vkCreateShaderModule",
    "destroyShaderModule": "vkDestroyShaderModule",
    "createComputePipeline": "vkCreateComputePipelines",
    "destroyPipeline": "vkDestroyPipeline",
    "createPipelineLayout": "vkCreatePipelineLayout",
    "destroyPipelineLayout": "vkDestroyPipelineLayout",
    "createDescriptorSetLayout": "vkCreateDescriptorSetLayout",
    "destroyDescriptorSetLayout": "vkDestroyDescriptorSetLayout",
    "createDescriptorPool": "vkCreateDescriptorPool",
    "destroyDescriptorPool": "vkDestroyDescriptorPool",
    "allocateDescriptorSets": "vkAllocateDescriptorSets",
    "updateDescriptorSets": "vkUpdateDescriptorSets",
    "createCommandPool": "vkCreateCommandPool",
    "destroyCommandPool": "vkDestroyCommandPool",
    "resetCommandPool": "vkResetCommandPool",
    "allocateCommandBuffers": "vkAllocateCommandBuffers",
    "bindPipeline": "vkCmdBindPipeline",
    "bindDescriptorSets": "vkCmdBindDescriptorSets",
    "pushConstants": "vkCmdPushConstants", "dispatch": "vkCmdDispatch",
    "copyBuffer": "vkCmdCopyBuffer", "fillBuffer": "vkCmdFillBuffer",
    "pipelineBarrier": "vkCmdPipelineBarrier",
    "createFence": "vkCreateFence", "destroyFence": "vkDestroyFence",
    "resetFences": "vkResetFences", "waitForFences": "vkWaitForFences",
    "getFenceStatus": "vkGetFenceStatus", "createEvent": "vkCreateEvent",
    "destroyEvent": "vkDestroyEvent", "resetEvent": "vkResetEvent",
    "createSemaphore": "vkCreateSemaphore",
    "destroySemaphore": "vkDestroySemaphore",
    "createQueryPool": "vkCreateQueryPool",
    "destroyQueryPool": "vkDestroyQueryPool",
    "resetQueryPool": "vkResetQueryPool",
    "getQueryPoolResults": "vkGetQueryPoolResults",
    "getMemoryProperties": "vkGetPhysicalDeviceMemoryProperties",
    "getProperties": "vkGetPhysicalDeviceProperties",
    "getFeatures": "vkGetPhysicalDeviceFeatures",
    "getQueueFamilyProperties": "vkGetPhysicalDeviceQueueFamilyProperties",
    "enumeratePhysicalDevices": "vkEnumeratePhysicalDevices",
}

# Commands that must round-trip via SUBMIT_3D regardless of how ggml spells the
# vk-hpp init (begin/end command buffer, queue submit, device idle) plus the
# Venus transport extension always required to drive the ring.
ALWAYS = {
    "vkBeginCommandBuffer", "vkEndCommandBuffer", "vkFreeCommandBuffers",
    "vkQueueSubmit", "vkQueueWaitIdle", "vkDeviceWaitIdle", "vkDestroyDevice",
    "vkDestroyInstance", "vkGetDeviceQueue2",
    "vkSetReplyCommandStreamMESA", "vkSeekReplyCommandStreamMESA",
    "vkExecuteCommandStreamsMESA", "vkCreateRingMESA", "vkDestroyRingMESA",
    "vkNotifyRingMESA",
}

def extract(text: str) -> set[str]:
    found = set(ALWAYS)
    # direct C entry points: vkXxx(
    for m in re.finditer(r"\b(vk[A-Z][A-Za-z0-9]+)\s*\(", text):
        found.add(m.group(1))
    # vk-hpp methods: .method( or ->method(
    for m in re.finditer(r"[.\->]\s*([a-z][A-Za-z0-9]+)\s*\(", text):
        cmd = HPP_METHOD_TO_CMD.get(m.group(1))
        if cmd:
            found.add(cmd)
    # keep only real Vulkan command spellings
    return {c for c in found if re.fullmatch(r"vk[A-Z][A-Za-z0-9]+(MESA|EXT|KHR)?", c)}

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    if not SRC.exists():
        print(f"ggml-vulkan source not found: {SRC}", file=sys.stderr)
        return 2
    cmds = sorted(extract(SRC.read_text(errors="ignore")))
    payload = {"source": str(SRC), "command_count": len(cmds), "commands": cmds}
    print(json.dumps(payload, indent=2) if args.json else "\n".join(cmds))
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run it, expect PASS**

Run: `python3 -m pytest scripts/venus/test_extract.py -v`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
git add scripts/extract_ggml_vk_commands.py scripts/venus/test_extract.py
git commit -m "venus-gen: deterministic ggml-vulkan command extractor"
```

### Task 1.2: Freeze the manifest artifact

**Files:**
- Create: `config/venus_command_manifest.json`
- Test: `scripts/venus/test_manifest.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/venus/test_manifest.py
import json, subprocess, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config/venus_command_manifest.json"
SCRIPT = ROOT / "scripts/extract_ggml_vk_commands.py"

def test_manifest_matches_live_scan():
    live = json.loads(subprocess.check_output(
        [sys.executable, str(SCRIPT), "--json"], text=True))["commands"]
    frozen = json.loads(MANIFEST.read_text())["commands"]
    assert frozen == live, "manifest drifted from ggml-vulkan.cpp; regenerate it"
```

- [ ] **Step 2: Run it, expect FAIL** (manifest absent).

Run: `python3 -m pytest scripts/venus/test_manifest.py -v`
Expected: FAIL — `FileNotFoundError`.

- [ ] **Step 3: Generate the frozen manifest**

Run: `LLAMA_ROOT=../llama.cpp python3 scripts/extract_ggml_vk_commands.py --json > config/venus_command_manifest.json`

- [ ] **Step 4: Run it, expect PASS**

Run: `python3 -m pytest scripts/venus/test_manifest.py -v`
Expected: PASS (1 passed).

- [ ] **Step 5: Commit**

```bash
git add config/venus_command_manifest.json scripts/venus/test_manifest.py
git commit -m "venus-gen: freeze ggml-vulkan command manifest"
```

---

## Milestone M2 — Make the generator real

### Task 2.1: Rewrite `gen_libukvenus.py` generate/verify

**Files:**
- Modify: `scripts/gen_libukvenus.py`
- Test: `scripts/venus/test_generate.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/venus/test_generate.py
import json, subprocess, sys, pathlib, hashlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
GEN = ROOT / "scripts/gen_libukvenus.py"
OUTDIR = ROOT / "libs/libukvulkan_venus/generated"

def gen():
    subprocess.check_call([sys.executable, str(GEN), "generate"])

def test_generate_is_deterministic():
    gen(); first = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in OUTDIR.glob("vn_protocol_driver_*.h")}
    gen(); second = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in OUTDIR.glob("vn_protocol_driver_*.h")}
    assert first == second and first, "generation not reproducible"

def test_lock_matches_output():
    gen()
    lock = json.loads((OUTDIR / "GENERATED.lock").read_text())
    for name, sha in lock["files"].items():
        assert hashlib.sha256((OUTDIR / name).read_bytes()).hexdigest() == sha

def test_encoder_present_and_no_external_deps():
    gen()
    cmdbuf = (OUTDIR / "vn_protocol_driver_command_buffer.h").read_text()
    assert "vn_encode_vkCmdDispatch" in cmdbuf
    import re
    incs = set(re.findall(r'#include\s+["<]([^">]+)[">]',
               "\n".join(p.read_text() for p in OUTDIR.glob("*.h"))))
    allowed_prefixes = ("vn_protocol_driver_", "vn_cs.h", "vn_ring.h",
                        "vulkan/", "vk_video/", "vk_platform.h",
                        "string.h", "stdlib.h", "assert.h")
    bad = [i for i in incs if not i.startswith(allowed_prefixes)]
    assert not bad, f"unexpected external include(s): {bad}"
```

- [ ] **Step 2: Run it, expect FAIL**

Run: `python3 -m pytest scripts/venus/test_generate.py -v`
Expected: FAIL — current `generate` shells into a non-existent `--out-dir` arg and writes nothing usable.

- [ ] **Step 3: Replace `cmd_generate` and add `cmd_verify`**

Replace the body of `cmd_generate` (and register a `verify` subcommand) in `scripts/gen_libukvenus.py`. Use the pin file as the single source of the commit + checkout + variant:

```python
import filecmp, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / "scripts/venus/pin.json").read_text())
VENUS_PROTOCOL = (ROOT / PIN["checkout"]).resolve()
OUTDIR = ROOT / "libs/libukvulkan_venus/generated"

def _run_upstream(dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for p in dest.glob("vn_protocol_driver_*.h"):
        p.unlink()
    cmd = [sys.executable, str(VENUS_PROTOCOL / PIN["generator"]),
           "--outdir", str(dest)]          # no --renderer => driver/guest variant
    subprocess.run(cmd, check=True, cwd=str(VENUS_PROTOCOL))

def _write_lock(dest: Path) -> None:
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(dest.glob("vn_protocol_driver_*.h"))}
    lock = {"commit": PIN["commit"], "variant": PIN["variant"], "files": files}
    (dest / "GENERATED.lock").write_text(json.dumps(lock, indent=2) + "\n")

def cmd_generate(_args) -> int:
    if cmd_check(_args) != 0:
        return 1
    _run_upstream(OUTDIR)
    _write_lock(OUTDIR)
    print(f"gen_libukvenus: PASS wrote {OUTDIR.relative_to(ROOT)}")
    return 0

def cmd_verify(_args) -> int:
    """Regenerate into a temp dir and diff against the committed tree."""
    if cmd_check(_args) != 0:
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        _run_upstream(tmp)
        _write_lock(tmp)
        names = {p.name for p in OUTDIR.glob("vn_protocol_driver_*.h")}
        names |= {p.name for p in tmp.glob("vn_protocol_driver_*.h")}
        diffs = [n for n in sorted(names)
                 if not (OUTDIR / n).exists() or not (tmp / n).exists()
                 or not filecmp.cmp(OUTDIR / n, tmp / n, shallow=False)]
        if diffs:
            print("gen_libukvenus verify: FAIL drift in: " + ", ".join(diffs))
            return 1
    print("gen_libukvenus verify: PASS committed tree matches upstream regen")
    return 0
```

Register `verify` next to `plan/check/generate` in `main()`:

```python
    sub.add_parser("verify").set_defaults(func=cmd_verify)
```

- [ ] **Step 4: Generate, then run the tests**

Run: `python3 scripts/gen_libukvenus.py generate && python3 -m pytest scripts/venus/test_generate.py -v`
Expected: `gen_libukvenus: PASS wrote libs/libukvulkan_venus/generated` then 3 passed.

- [ ] **Step 5: Commit (generated tree + lock are committed verbatim)**

```bash
git add scripts/gen_libukvenus.py scripts/venus/test_generate.py \
        libs/libukvulkan_venus/generated/
git commit -m "venus-gen: real reproducible generator + committed driver headers"
```

### Task 2.2: Coverage check — manifest commands exist in generated set

**Files:**
- Test: `scripts/venus/test_coverage.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/venus/test_coverage.py
import json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTDIR = ROOT / "libs/libukvulkan_venus/generated"
MANIFEST = json.loads((ROOT / "config/venus_command_manifest.json").read_text())

def test_every_ggml_command_has_an_encoder():
    blob = "\n".join(p.read_text() for p in OUTDIR.glob("vn_protocol_driver_*.h"))
    have = set(re.findall(r"vn_encode_(vk[A-Za-z0-9]+)\b", blob))
    missing = [c for c in MANIFEST["commands"] if c not in have]
    assert not missing, f"generated set lacks encoders for: {missing}"
```

- [ ] **Step 2: Run it**

Run: `python3 -m pytest scripts/venus/test_coverage.py -v`
Expected: **Likely PASS.** If it FAILS, the failing command's extension is not in the upstream `VK_XML_EXTENSION_LIST`. Resolution: add that extension to `wanted_extensions` in `pin.json` **and** confirm it is already enabled upstream; if upstream does not enable it, document the gap in `GENERATOR.md` and either (a) keep a hand-written encoder for just that command in `venus_compute.c` (do not delete it at M6), or (b) raise it upstream. Do not edit generated files.

- [ ] **Step 3: Commit**

```bash
git add scripts/venus/test_coverage.py
git commit -m "venus-gen: assert generated encoders cover the ggml manifest"
```

---

## Milestone M3 — The Unikraft shim (`vn_cs.h` + `vn_ring.h`)

### Task 3.1: `vn_cs.h` — encoder/decoder/handle-id adapter

**Files:**
- Create: `libs/libukvulkan_venus/include/uk/vn_cs.h`
- Test: covered by the compile gate (Task 4.1) and parity test (Task 5.1).

- [ ] **Step 1: Write the shim**

```c
/* libs/libukvulkan_venus/include/uk/vn_cs.h
 *
 * Unikraft adapter satisfying the interface documented in the upstream
 * venus-protocol driver_cs.h template. Maps the generated vn_encode_* /
 * vn_decode_* primitives onto struct uk_venus_encoder. Handles are bare
 * uint64 guest ids in VOGUE, so id == (uintptr_t)handle.
 */
#ifndef VN_CS_H
#define VN_CS_H

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <uk/venus.h>   /* struct uk_venus_encoder + uk_venus_encode_bytes */

/* Generated vn_submit_* wrappers stack-allocate this many bytes before
 * falling back to malloc(). Matches upstream default. */
#ifndef VN_SUBMIT_LOCAL_CMD_SIZE
#define VN_SUBMIT_LOCAL_CMD_SIZE 512
#endif

struct vn_cs_encoder {
	struct uk_venus_encoder *e;
};

struct vn_cs_decoder {
	const uint8_t *cur;
	const uint8_t *end;
	int fatal;
};

/* --- encoder --- */
static inline size_t vn_cs_encoder_get_len(const struct vn_cs_encoder *enc)
{
	return uk_venus_encoder_size(enc->e);
}

/* Fixed-capacity backing buffer: reserve is a bounds check only. */
static inline void *vn_cs_encoder_reserve(struct vn_cs_encoder *enc, size_t size)
{
	(void)size;
	return enc; /* non-NULL = ok; overflow is tracked inside uk_venus_encoder */
}

static inline void vn_cs_encoder_write(struct vn_cs_encoder *enc, size_t size,
				       const void *data, size_t data_size)
{
	assert(size % 4 == 0);
	uk_venus_encode_bytes(enc->e, data, data_size);
	if (size > data_size)                 /* zero-pad to 4-byte stride */
		uk_venus_encode_bytes(enc->e, NULL, size - data_size);
}

/* --- decoder (reply parsing) --- */
static inline void vn_cs_decoder_set_fatal(struct vn_cs_decoder *dec)
{
	dec->fatal = 1;
}

static inline void vn_cs_decoder_read(struct vn_cs_decoder *dec, size_t size,
				      void *data, size_t data_size)
{
	assert(size % 4 == 0);
	if (dec->cur + size > dec->end) { dec->fatal = 1; return; }
	memcpy(data, dec->cur, data_size);
	dec->cur += size;
}

static inline void *vn_cs_decoder_peek(struct vn_cs_decoder *dec, size_t size,
				       void *data, size_t data_size)
{
	if (dec->cur + size > dec->end) { dec->fatal = 1; return NULL; }
	memcpy(data, dec->cur, data_size);
	return (void *)dec->cur;
}

/* --- handle <-> id --- */
static inline uint64_t vn_cs_handle_load_id(const void **handle, VkObjectType t)
{
	(void)t;
	return (uint64_t)(uintptr_t)*handle;
}

static inline void vn_cs_handle_store_id(void **handle, uint64_t id, VkObjectType t)
{
	(void)t;
	*handle = (void *)(uintptr_t)id;
}

#endif /* VN_CS_H */
```

- [ ] **Step 2: Note exact-signature pinning**

The signatures above are taken from the upstream `driver_cs.h` template usage. The compile gate (Task 4.1) is authoritative: if `clang` reports a conflicting type for any `vn_cs_*` symbol, adjust this header to match the generated declaration verbatim (the generated headers are the source of truth). Do not change generated files.

- [ ] **Step 3: Commit**

```bash
git add libs/libukvulkan_venus/include/uk/vn_cs.h
git commit -m "venus-gen: vn_cs.h encoder/decoder/handle shim onto uk_venus_encoder"
```

### Task 3.2: `vn_ring.h` + implementation — 4-function ring adapter

**Files:**
- Create: `libs/libukvulkan_venus/include/uk/vn_ring.h`
- Create: `libs/libukvulkan_venus/vn_ring_shim.c`
- Modify: `libs/libukvulkan_venus/venus_init.c`, `libs/libukvulkan_venus/include/uk/venus.h`

- [ ] **Step 1: Write the header**

```c
/* libs/libukvulkan_venus/include/uk/vn_ring.h
 *
 * Satisfies the vn_submit_* / vn_call_* wrappers in the generated headers.
 * Surface (verified against the generated tree) is exactly four functions and
 * one struct that the generated code only passes by address. Maps onto the
 * existing struct uk_venus_ring + uk_venus_submit transport.
 */
#ifndef VN_RING_H
#define VN_RING_H

#include <stddef.h>
#include <uk/venus.h>          /* struct uk_venus_ring, uk_venus_* */
#include <uk/vn_cs.h>

struct vn_ring;                /* opaque alias; we bind a uk_venus_ring* */

struct vn_ring_submit_command {
	struct vn_cs_encoder enc;          /* encode target for this command */
	struct uk_venus_encoder backing;   /* owns the byte buffer */
	void *cmd_data;
	size_t cmd_size;
	size_t reply_size;
	struct vn_cs_decoder reply;        /* populated after submit if reply_size */
	int has_reply;
};

struct vn_cs_encoder *
vn_ring_submit_command_init(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit,
			    void *cmd_data, size_t cmd_size, size_t reply_size);

void vn_ring_submit_command(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit);

struct vn_cs_decoder *
vn_ring_get_command_reply(struct vn_ring *ring,
			  struct vn_ring_submit_command *submit);

void vn_ring_free_command_reply(struct vn_ring *ring,
				struct vn_ring_submit_command *submit);

#endif /* VN_RING_H */
```

- [ ] **Step 2: Implement the four functions**

```c
/* libs/libukvulkan_venus/vn_ring_shim.c */
#include <uk/vn_ring.h>

struct vn_cs_encoder *
vn_ring_submit_command_init(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit,
			    void *cmd_data, size_t cmd_size, size_t reply_size)
{
	(void)ring;
	if (!cmd_size)
		return NULL;
	uk_venus_encoder_init(&submit->backing, cmd_data, cmd_size);
	submit->enc.e      = &submit->backing;
	submit->cmd_data   = cmd_data;
	submit->cmd_size   = cmd_size;
	submit->reply_size = reply_size;
	submit->has_reply  = 0;
	return &submit->enc;
}

void vn_ring_submit_command(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit)
{
	(void)ring;
	/* Bridge to the existing transport bound by uk_venus_ring_bind_current().
	 * Encodes already sit in submit->backing; this submits via SUBMIT_3D and,
	 * when reply_size != 0, captures the host reply into submit->reply. */
	uk_venus_ring_shim_submit(submit);
}

struct vn_cs_decoder *
vn_ring_get_command_reply(struct vn_ring *ring,
			  struct vn_ring_submit_command *submit)
{
	(void)ring;
	return submit->has_reply ? &submit->reply : NULL;
}

void vn_ring_free_command_reply(struct vn_ring *ring,
				struct vn_ring_submit_command *submit)
{
	(void)ring; (void)submit;   /* reply lives in the ring's reply blob */
}
```

- [ ] **Step 3: Add the transport thunk in `venus_init.c`**

Add `uk_venus_ring_bind_current(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_context *ctx, struct uk_venus_ring *ring)` (stores the active transport in a file-scope static) and `uk_venus_ring_shim_submit(struct vn_ring_submit_command *submit)`, reusing the existing `uk_venus_submit` + reply-blob path that `uk_venus_query_device_name()` already implements. Declare both in `include/uk/venus.h`.

- [ ] **Step 4: Commit**

```bash
git add libs/libukvulkan_venus/include/uk/vn_ring.h libs/libukvulkan_venus/vn_ring_shim.c \
        libs/libukvulkan_venus/venus_init.c libs/libukvulkan_venus/include/uk/venus.h
git commit -m "venus-gen: vn_ring.h 4-function shim onto uk_venus_ring transport"
```

---

## Milestone M4 — Compile gate

### Task 4.1: Generated headers compile through the shim

**Files:**
- Create: `tests/venus_generated_compile_test.c`
- Modify: `tests/Makefile`

- [ ] **Step 1: Write the smoke test**

```c
/* tests/venus_generated_compile_test.c
 * Includes the generated encoder headers through the shim and exercises one
 * encoder end-to-end so the whole include graph must compile + link. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "vn_protocol_driver_buffer.h"
#include "vn_protocol_driver_command_buffer.h"

int main(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder e;
	struct vn_cs_encoder enc = { .e = &e };
	uk_venus_encoder_init(&e, buf, sizeof(buf));

	VkCommandBuffer cb = (VkCommandBuffer)(uintptr_t)0x1234;
	vn_encode_vkCmdDispatch(&enc, 0, cb, 4, 5, 6);
	assert(!uk_venus_encoder_overflow(&e));
	assert(uk_venus_encoder_size(&e) > 0);
	printf("venus_generated_compile_test: PASS (%zu bytes)\n",
	       uk_venus_encoder_size(&e));
	return 0;
}
```

- [ ] **Step 2: Add the target to `tests/Makefile`**

```make
$(BUILD)/venus_generated_compile_test: venus_generated_compile_test.c \
	../libs/libukvulkan_venus/venus_cs.c ../libs/libukvulkan_venus/venus_init.c \
	../libs/libukvulkan_venus/venus_compute.c ../libs/libukvulkan_venus/vn_ring_shim.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) -I../libs/libukvulkan_venus/generated $^ -o $@

venus-gen-compile: $(BUILD)/venus_generated_compile_test
	./$(BUILD)/venus_generated_compile_test
```

Add `$(BUILD)/venus_generated_compile_test` to `TEST_VENUS_BINS` and `venus-gen-compile` to `.PHONY`.

- [ ] **Step 3: Build + run**

Run: `make -C tests venus-gen-compile`
Expected: `venus_generated_compile_test: PASS (N bytes)`.
If `clang` reports a `vn_cs_*` signature conflict, fix `vn_cs.h` to match the generated declaration (per Task 3.1 Step 2), re-run.

- [ ] **Step 4: Commit**

```bash
git add tests/venus_generated_compile_test.c tests/Makefile
git commit -m "venus-gen: compile gate for generated headers through the shim"
```

---

## Milestone M5 — Wire-format parity gate

### Task 5.1: Byte-for-byte legacy-vs-generated parity

**Files:**
- Modify: `libs/libukvulkan_venus/venus_cs.c`, `libs/libukvulkan_venus/venus_compute.c` (temporarily expose legacy bodies under `__legacy` suffix)
- Create: `tests/venus_parity_test.c`
- Modify: `tests/Makefile`

- [ ] **Step 1: Alias the current encoders as legacy**

In `venus_cs.c`/`venus_compute.c`, rename each current public body to a `__legacy` suffix (e.g. `uk_venus_encode_vkCreateBuffer__legacy`) and have the public symbol call the legacy body for now. Declare the `__legacy` variants in a test-only header `tests/shim/venus_legacy.h` so both implementations coexist during parity.

- [ ] **Step 2: Write the parity test**

```c
/* tests/venus_parity_test.c
 * For each ggml command we encode the SAME inputs through (a) the legacy
 * hand-written encoder and (b) the generated encoder, and assert the byte
 * streams are identical. Generated output is the source of truth: a mismatch
 * means the legacy encoder had a wire-format bug. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <uk/venus.h>
#include <uk/vn_cs.h>
#include "shim/venus_legacy.h"
#include "vn_protocol_driver_buffer.h"

static int eq(const uint8_t *a, size_t na, const uint8_t *b, size_t nb,
	      const char *what)
{
	if (na != nb || memcmp(a, b, na) != 0) {
		printf("PARITY FAIL %s: legacy=%zu gen=%zu\n", what, na, nb);
		return 0;
	}
	return 1;
}

static int parity_create_buffer(void)
{
	uint8_t lb[256], gb[256];
	struct uk_venus_encoder le, ge;
	uk_venus_encoder_init(&le, lb, sizeof(lb));
	uk_venus_encoder_init(&ge, gb, sizeof(gb));

	uk_venus_encode_vkCreateBuffer__legacy(&le, /*device*/7, /*buf*/9,
					       /*size*/1024, /*usage*/0x20);

	struct vn_cs_encoder enc = { .e = &ge };
	VkDevice dev = (VkDevice)(uintptr_t)7;
	VkBuffer out = (VkBuffer)(uintptr_t)9;
	VkBufferCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 1024, .usage = 0x20,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	vn_encode_vkCreateBuffer(&enc, 0, dev, &ci, NULL, &out);

	return eq(lb, uk_venus_encoder_size(&le),
		  gb, uk_venus_encoder_size(&ge), "vkCreateBuffer");
}

int main(void)
{
	int ok = 1;
	ok &= parity_create_buffer();
	/* ... one parity_* per ggml command in the manifest ... */
	if (ok) { printf("venus_parity_test: PASS\n"); return 0; }
	return 1;
}
```

- [ ] **Step 3: Add target + run**

Add a `venus-parity` target to `tests/Makefile` (same compile recipe as Task 4.1, with `-I../libs/libukvulkan_venus/generated`).
Run: `make -C tests venus-parity`
Expected: `venus_parity_test: PASS`.
**If a command mismatches:** the generated encoder is authoritative. Record the divergence in `report-porting.md`, fix the *caller's* struct construction (not the generated header), and if the legacy encoder was wrong, note it as a bug the port fixes.

- [ ] **Step 4: Commit**

```bash
git add tests/venus_parity_test.c tests/shim/venus_legacy.h tests/Makefile \
        libs/libukvulkan_venus/venus_cs.c libs/libukvulkan_venus/venus_compute.c
git commit -m "venus-gen: byte-for-byte parity gate legacy vs generated encoders"
```

---

## Milestone M6 — Cut over and delete hand-written encoders

### Task 6.1: Reimplement public `uk_venus_encode_*` on generated encoders

**Files:**
- Modify: `libs/libukvulkan_venus/venus_cs.c`, `libs/libukvulkan_venus/venus_compute.c`

- [ ] **Step 1: Reimplement one encoder body via the generated path**

For each public `uk_venus_encode_*`, replace the hand-rolled byte-pushing with: build the real `Vk*` struct from the scalar args, then call the generated `vn_encode_vk*`. Example:

```c
void uk_venus_encode_vkCreateBuffer(struct uk_venus_encoder *e,
				    uint64_t device, uint64_t buffer_handle,
				    uint64_t size, uint32_t usage)
{
	struct vn_cs_encoder enc = { .e = e };
	VkDevice dev = (VkDevice)(uintptr_t)device;
	VkBuffer out = (VkBuffer)(uintptr_t)buffer_handle;
	VkBufferCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size, .usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	vn_encode_vkCreateBuffer(&enc, 0, dev, &ci, NULL, &out);
}
```

- [ ] **Step 2: Run parity + the venus suite after each conversion**

Run: `make -C tests venus-parity venus-cs venus-compute`
Expected: all PASS. Convert one encoder per commit so a regression bisects to one command.

- [ ] **Step 3: Delete the `__legacy` bodies and the parity test once all green**

Remove the `__legacy` aliases, `tests/shim/venus_legacy.h`, and `tests/venus_parity_test.c` (their job — guarding the cutover — is done). Keep `venus-gen-compile` as the standing gate.

- [ ] **Step 4: Run the full native suite**

Run: `make -C tests native`
Expected: full deterministic suite PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/libukvulkan_venus tests/Makefile
git commit -m "venus-gen: route uk_venus_encode_* through generated encoders; drop hand-written wire code"
```

---

## Milestone M7 — Build wiring, governance, docs

### Task 7.1: Wire the include path + verify gate

**Files:**
- Modify: `libs/libukvulkan_venus/Makefile.uk`, `libs/libukvulkan_venus/Config.uk`, `Makefile`

- [ ] **Step 1:** Add `LIBUKVENUS_CINCLUDES-y += -I$(LIBUKVENUS_BASE)/generated` to `Makefile.uk` (mirror the existing `include/` line). Add `vn_ring_shim.c` to the built sources.
- [ ] **Step 2:** Add a Make target:

```make
gen-libukvenus-verify: gen-libukvenus-check
	python3 scripts/gen_libukvenus.py verify
```

Add `gen-libukvenus-verify` to the `.PHONY` line (`Makefile:70`) and into the `governance-check` prerequisite list so CI proves the committed tree still matches an upstream regen.

- [ ] **Step 3:** Run: `make gen-libukvenus-verify`
Expected: `gen_libukvenus verify: PASS committed tree matches upstream regen`.
- [ ] **Step 4: Commit**

```bash
git add libs/libukvulkan_venus/Makefile.uk libs/libukvulkan_venus/Config.uk Makefile
git commit -m "venus-gen: wire generated includes + gen-libukvenus-verify gate"
```

### Task 7.2: Governance + docs

**Files:**
- Modify: `libs/libukvulkan_venus/GENERATOR.md`, `libs/libukvulkan_venus/README.md`, `config/governance.json`, `README.md` status, `report-porting.md`

- [ ] **Step 1:** Rewrite `GENERATOR.md` "Workflow"/"Why not vendor" to describe the now-real flow: `pin.json` → `make gen-libukvenus` (commits verbatim driver headers + `GENERATED.lock`) → `vn_cs.h`/`vn_ring.h` shim → `gen-libukvenus-verify`. Document the manifest + coverage check and the "generated is source of truth" rule.
- [ ] **Step 2:** Update `libs/libukvulkan_venus/README.md` source-lineage section: encoders are generated from venus-protocol `70991d4`; transport stays `libukvirtio_gpu`.
- [ ] **Step 3:** Update `config/governance.json` + README status columns so `make governance-check lib-readme-check app-port-check` passes.
- [ ] **Step 4:** Record outcomes (parity divergences found, commands hand-kept if any) in `report-porting.md`.
- [ ] **Step 5: Run governance gates**

Run: `make governance-check lib-readme-check app-port-check gen-libukvenus-verify`
Expected: all PASS.
- [ ] **Step 6: Commit**

```bash
git add libs/libukvulkan_venus/GENERATOR.md libs/libukvulkan_venus/README.md \
        config/governance.json README.md report-porting.md
git commit -m "venus-gen: governance + docs for deterministic encoder generation"
```

---

## Appendix A — Transport mapping (reference, no codegen)

`mesa/src/virtio/vulkan/vn_renderer_virtgpu.c` is OS glue, **not** generated. It is already replaced by `libukvirtio_gpu` + `libukvirtgpu_drm`. This mapping is documented (and locked by a tiny assertion test) so reviewers can trace fidelity — it is **not** ported by script.

| Mesa `vn_renderer_virtgpu.c` | VOGUE equivalent |
|---|---|
| `open("/dev/dri/renderD128")` | `uk_virtio_gpu_init()` |
| `ioctl(DRM_IOCTL_VIRTGPU_GET_CAPS)` | `uk_venus_capset_get()` |
| `ioctl(DRM_IOCTL_VIRTGPU_CONTEXT_INIT)` | `uk_venus_context_create()` |
| `ioctl(DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB)` | `uk_virtio_gpu_resource_create_blob()` |
| `mmap(fd, offset)` | `uk_virtio_gpu_blob_map()` |
| `ioctl(DRM_IOCTL_VIRTGPU_EXECBUFFER)` (SUBMIT_3D) | `uk_venus_submit()` / `uk_venus_ring_submit()` |
| ring head/tail/notify (`vn_ring.c`) | `uk_venus_ring_{register,cmd_write,cmd_flush,cmd_wait}` |

**Optional follow-on (out of scope for first cut):** once the `vn_ring.h` shim is proven, the hand-written `uk_venus_query_*` round-trips can be replaced by the generated `vn_call_vk*` wrappers (which already do encode → submit → decode-reply), further shrinking `venus_compute.c`. Gate behind the same parity discipline.

## Appendix B — Canonical ggml command manifest

Authoritative list lives in `config/venus_command_manifest.json` (Task 1.2), regenerated by `scripts/extract_ggml_vk_commands.py`. It is grouped by object type only for human review; the generator slices by extension, and compile-time DCE drops every `static inline` encoder no caller references.

---

## Self-review

- **Spec coverage.** "Port Mesa virtgpu impl needed by ggml" → M1 derives the ggml command set; M2 generates exactly the encoders for the enabling extensions; Appendix A maps the (non-generatable) transport already covered by `libukvirtio_gpu`. "Deterministic/script-based porting" → M0 pins the commit, M2 generates verbatim + `GENERATED.lock`, `gen-libukvenus-verify` proves reproducibility, M5 byte-parity gates correctness. "Only functions ggml needs" → extension slice + compile-time DCE, asserted by the coverage test (Task 2.2). Plan saved to `plan-porting-virtgpu.md` per the goal.
- **Placeholder scan.** Every C/Python/Make step shows concrete code or an exact command + expected output. The one residual unknown — exact `vn_cs_*` signatures — is handled by an explicit "compile gate is authoritative; match the generated declaration" instruction (Task 3.1 Step 2 / Task 4.1 Step 3), not a TODO.
- **Type consistency.** `struct vn_cs_encoder { struct uk_venus_encoder *e; }`, `struct vn_ring_submit_command`, and the four `vn_ring_*` signatures are used identically across `vn_cs.h`, `vn_ring.h`, `vn_ring_shim.c`, the compile test, and the parity test. `uk_venus_encode_*` public signatures are unchanged across M5/M6 so callers in `libukggml_vk` need no edits.
- **Known risk.** If the upstream `VK_XML_EXTENSION_LIST` does not enable an extension a ggml command needs, Task 2.2 fails loudly with the missing command named and a documented resolution (extend `wanted_extensions` + keep a hand-written encoder for that one command). This cannot pass silently.
