#!/usr/bin/env python3
"""Ensure native Vulkan/llama builds do not select the DRM compatibility shim."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]

NATIVE_FILES = [
    ROOT / "apps" / "app-llama-upstream-vk" / "Config.uk",
    ROOT / "apps" / "app-vkmark" / "Config.uk",
    ROOT / "libs" / "libvulkan" / "Config.uk",
    ROOT / "kraft" / "Kraftfile.llama-upstream-vk",
    ROOT / "kraft" / "Kraftfile.llama-upstream-vk-server",
]

FORBIDDEN_KRAFT = [
    "CONFIG_LIBUKVIRTGPU_DRM: 'y'",
    "CONFIG_LIBUKVIRTGPU_DRM: \"y\"",
    "CONFIG_LIBUKVIRTGPU_DRM_FDIO: 'y'",
    "CONFIG_LIBUKVIRTGPU_DRM_FDIO: \"y\"",
    "CONFIG_LIBVULKAN_ENABLE_DRM_FD_COMPAT: 'y'",
    "CONFIG_LIBVULKAN_ENABLE_DRM_FD_COMPAT: \"y\"",
]

ALLOWED_DRM_SELECTOR_CONFIGS = {
    "LIBVULKAN_ENABLE_DRM_FD_COMPAT",
    "LIBUKVULKAN_VENUS_USE_DRM_COMPAT",
}


def check_kconfig(path: Path) -> list[str]:
    failures: list[str] = []
    current_config = ""

    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if line.startswith("config ") or line.startswith("menuconfig "):
            current_config = line.split()[1]
            continue

        if line in {"select LIBUKVIRTGPU_DRM", "select LIBUKVIRTGPU_DRM_FDIO"}:
            if current_config not in ALLOWED_DRM_SELECTOR_CONFIGS:
                failures.append(f"{path.relative_to(ROOT)}:{lineno}: {line}")

    return failures


def main() -> int:
    failures: list[str] = []
    for path in NATIVE_FILES:
        text = path.read_text()
        rel = path.relative_to(ROOT)
        for needle in FORBIDDEN_KRAFT:
            if needle in text:
                failures.append(f"{rel}: contains {needle}")
        if path.suffix == ".uk":
            failures.extend(check_kconfig(path))

    if failures:
        print("native-vulkan-no-drm-check: FAIL")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print("native-vulkan-no-drm-check: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
