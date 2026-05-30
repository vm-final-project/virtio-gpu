#!/usr/bin/env python3
"""Static implementation gate for the real VirtIO-GPU backend.

This does not replace QEMU execution.  It prevents regressions where the real
backend silently falls back to milestone stubs for commands that the v1 plan
requires to be real controlq operations.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REAL = ROOT / "libs/libukvirtio_gpu/virtio_gpu_real.c"
PROTO = ROOT / "libs/libukvirtio_gpu/virtio_gpu_proto.h"
text = REAL.read_text()
proto = PROTO.read_text()
errors: list[str] = []

required_commands = [
    "UKVGPU_CMD_RESOURCE_CREATE_3D",
    "UKVGPU_CMD_TRANSFER_TO_HOST_3D",
    "UKVGPU_CMD_TRANSFER_FROM_HOST_3D",
    "UKVGPU_CMD_CTX_CREATE",
    "UKVGPU_CMD_CTX_DESTROY",
    "UKVGPU_CMD_CTX_ATTACH_RESOURCE",
    "UKVGPU_CMD_CTX_DETACH_RESOURCE",
    "UKVGPU_CMD_SUBMIT_3D",
    "UKVGPU_CMD_RESOURCE_CREATE_BLOB",
    "UKVGPU_CMD_RESOURCE_MAP_BLOB",
    "UKVGPU_CMD_RESOURCE_UNMAP_BLOB",
]
for token in required_commands:
    if token not in text:
        errors.append(f"real backend does not issue {token}")

stub_funcs = [
    "uk_virtio_gpu_gl_resource_create_3d",
    "uk_virtio_gpu_gl_context_create",
    "uk_virtio_gpu_gl_context_submit",
    "uk_virtio_gpu_gl_blob_create",
    "uk_virtio_gpu_gl_blob_map",
]
for fn in stub_funcs:
    m = re.search(rf"int\s+{fn}\s*\([^)]*\)\s*\{{(?P<body>.*?)\n\}}", text, re.S)
    if not m:
        errors.append(f"missing function {fn}")
        continue
    body = m.group("body")
    if "cmd_submit" not in body:
        errors.append(f"{fn} does not submit a real controlq command")
    compact = " ".join(body.split())
    if compact.startswith("(void)"):
        errors.append(f"{fn} still looks like a top-level stub")

for token in [
    "VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB",
    "VIRTIO_GPU_CMD_SUBMIT_3D",
    "VIRTIO_GPU_RESP_OK_MAP_INFO",
    "VIRTIO_GPU_BLOB_MEM_HOST3D",
    "VIRTIO_GPU_MAP_CACHE_WC",
]:
    if token not in proto:
        errors.append(f"protocol header missing {token}")

if "Current local Unikraft virtio-pci exposes no shared-memory BAR helper" not in text:
    errors.append("real blob_map must document fail-closed shared-memory-bar blocker")

if errors:
    print("FAIL: real driver static gate", file=sys.stderr)
    for e in errors:
        print(f"- {e}", file=sys.stderr)
    sys.exit(1)
print("PASS: real driver static gate")
