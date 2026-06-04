#!/usr/bin/env python3
"""Documentation/source-grounding gate for the VirtIO-GPU Venus/Vulkan v1 plan.

SOURCE_CHECKS validate external upstream repo headers when those repos are
present as siblings of vm-final-project/.  They are informational (warnings,
not errors) when the external repos are absent — the gate passes either way.
This makes the check portable across developer machines and CI environments
that only clone vm-final-project without the full workspace.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
CHECKS = {
    ROOT / "design/unikraft-virtio-gpu-spec-v1.md": [
        "Unikraft VirtIO-GPU Venus/Vulkan API Specification v1",
        "Official Unikraft compliance checklist",
        "VIRTIO_GPU_F_RESOURCE_BLOB",
        "VIRTIO_GPU_CAPSET_VENUS == 4",
        "libukvirtgpu_drm",
        "uk_vgpu_probe",
        "Out of scope",
    ],
    ROOT / "design/virtio-gpu-vulken-v1.md": [
        "Research-to-implementation traceability",
        "Gate proto.api-contract",
        "Gate G8",
        "performance evaluation",
        "submit latency p50/p95/p99",
        "Out of scope",
    ],
    ROOT / "README.md": [
        "VirtIO-GPU Venus/Vulkan v1 roadmap",
        "design/unikraft-virtio-gpu-spec-v1.md",
        "design/virtio-gpu-vulken-v1.md",
        "libvulkan -> libukvulkan_venus -> libukvirtio_gpu",
        "CONFIG_LIBVULKAN_ENABLE_DRM_FD_COMPAT",
        "future Mesa/Linux-style compatibility",
        "Out of scope",
    ],
    ROOT / "libs/libvulkan/README.md": [
        "application-facing Vulkan ABI/runtime boundary",
        "CONFIG_LIBVULKAN_ENABLE_DRM_FD_COMPAT",
        "Do not enable it for native",
        "llama.cpp/ggml-vulkan builds",
    ],
    ROOT / "libs/libukvulkan_venus/README.md": [
        "llama.cpp/ggml-vulkan builds use the native",
        "must not depend on the DRM shim",
    ],
    ROOT / "libs/libukvirtgpu_drm/README.md": [
        "vk.drm-core",
        "vk.drm-fdio",
        "syncobj and PRIME/dma-buf remain explicitly unsupported",
    ],
}

# External upstream source grounding checks.  Each entry maps a sibling-repo
# path to tokens that must appear when the file exists.  Missing files produce
# a warning but do NOT fail the gate — the workspace may not include all repos.
SOURCE_CHECKS = {
    REPO / "linux-version/linux-6.18/include/uapi/linux/virtio_gpu.h": [
        "VIRTIO_GPU_F_RESOURCE_BLOB", "VIRTIO_GPU_F_CONTEXT_INIT", "VIRTIO_GPU_CAPSET_VENUS"
    ],
    REPO / "linux-version/linux-6.18/include/uapi/drm/virtgpu_drm.h": [
        "DRM_IOCTL_VIRTGPU_GETPARAM", "DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB", "DRM_IOCTL_VIRTGPU_CONTEXT_INIT"
    ],
    REPO / "qemu-version/qemu-11.0/docs/system/devices/virtio/virtio-gpu.rst": [
        "hostmem", "blob=true", "venus=true"
    ],
    REPO / "mesa/docs/drivers/venus.rst": [
        "VIRTGPU_PARAM_3D_FEATURES", "VIRTGPU_PARAM_RESOURCE_BLOB", "VIRTGPU_PARAM_HOST_VISIBLE"
    ],
    REPO / "mesa/src/virtio/vulkan/vn_renderer_virtgpu.c": [
        "DRM_IOCTL_VIRTGPU_GETPARAM", "DRM_IOCTL_VIRTGPU_EXECBUFFER", "DRM_IOCTL_VIRTGPU_MAP"
    ],
    REPO / "venus-protocol/meson.build": ["venus-protocol", "VK_MESA_venus_protocol.xml"],
    REPO / "unikraft/drivers/virtio/include/virtio/virtio_ids.h": ["VIRTIO_ID_GPU"],
    REPO / "unikraft/lib/ukalloc/Config.uk": ["LIBUKALLOC"],
    REPO / "unikraft/lib/posix-mmap/Config.uk": ["POSIX_MMAP"],
}


def check_file(path: Path, needles: list, label: str) -> list:
    if not path.exists():
        return [f"missing {label}: {path}"]
    text = path.read_text(errors="replace")
    return [f"{path}: missing token {needle!r}" for needle in needles if needle not in text]


errors: list = []
for p, needles in CHECKS.items():
    errors.extend(check_file(p, needles, "doc"))

# Source checks: only fail when file exists but required token is absent.
# Log missing files as informational only.
source_warnings: list = []
source_errors: list = []
for p, needles in SOURCE_CHECKS.items():
    results = check_file(p, needles, "source")
    for r in results:
        if r.startswith("missing source:"):
            source_warnings.append(r)
        else:
            source_errors.append(r)

errors.extend(source_errors)

if source_warnings:
    print("INFO: some upstream source repos absent (informational, not blocking):")
    for w in source_warnings:
        print(f"  {w}")

if errors:
    print("FAIL: Venus/Vulkan documentation gate", file=sys.stderr)
    for err in errors:
        print(f"- {err}", file=sys.stderr)
    sys.exit(1)
print("PASS: Venus/Vulkan documentation gate")
