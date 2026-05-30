/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * libukvk_icd — Vulkan ICD shim for Unikraft (vk.icd gate)
 *
 * Provides the Vulkan Installable Client Driver (ICD) bootstrap layer
 * that sits above vk.drm-shim (libukvirtgpu_drm). Initializes a Venus/Vulkan
 * context over VirtIO-GPU, probes capsets, and exposes the device
 * information surface expected by Vulkan benchmark ports (app-vkmark).
 *
 * Architecture:
 *   Vulkan app
 *     -> uk_vulkan_icd_init()        [vk.icd: this library]
 *       -> uk_drm_virtgpu_open()     [vk.drm-shim: libukvirtgpu_drm]
 *         -> uk_virtio_gpu_probe()   [libukvirtio_gpu]
 *
 * Claim boundary: ICD initialization and Venus context creation PASS.
 * Vulkan rendering requires a ring-buffer setup plus same-run frame proof, which
 * is not yet implemented. Rendering paths are documented as BLOCKED.
 *
 * Source lineage:
 *   Mesa: src/virtio/vulkan/vn_device.c (Venus VkDevice init)
 *   Mesa: src/virtio/vulkan/vn_renderer_virtgpu.c (ICD renderer)
 *   venus-protocol: include/venus/vn_protocol_driver.h
 */
#ifndef UK_VULKAN_ICD_H
#define UK_VULKAN_ICD_H

#include <stdint.h>
#include <stddef.h>

#include <uk/drm_virtgpu.h>
#include <drm/virtgpu_drm.h>

/* ── ICD device info ─────────────────────────────────────────────────────── */

#define UK_VULKAN_ICD_MAX_NAME 64

struct uk_vulkan_icd_info {
    char     device_name[UK_VULKAN_ICD_MAX_NAME]; /* "virtio-gpu-venus" */
    uint32_t capset_id;           /* VIRTGPU_CAPSET_VENUS = 4 */
    int      venus_available;     /* capset id=4 found in supported set */
    int      has_resource_blob;   /* blob memory extension available */
    int      has_host_visible;    /* host-coherent memory available */
    int      has_context_init;    /* Venus context init supported */
    uint64_t supported_capsets;   /* bitmask from GETPARAM */

    /* Rendering blocked until ring-buffer/frame-proof path is implemented */
    const char *rendering_status; /* "blocked:ring-buffer-frame-proof-missing" */
};

/* ── ICD device handle ───────────────────────────────────────────────────── */

struct uk_vulkan_icd_dev {
    struct uk_drm_virtgpu_dev drm;  /* vk.drm-shim DRM virtgpu device (owns context) */
    struct uk_vulkan_icd_info info; /* device capability snapshot */

    int icd_initialized;            /* uk_vulkan_icd_init() succeeded */
    int ctx_initialized;            /* Venus context open via vk.drm-shim */
};

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * Initialize the Vulkan ICD shim over the VirtIO-GPU device at index
 * gpu_idx. Opens the vk.drm-shim DRM virtgpu device, probes Venus capset support,
 * and initializes a Venus rendering context.
 *
 * On success: returns 0, fills icd->info with device capabilities.
 * On error:   returns -errno; icd->icd_initialized is 0.
 *
 * Claim boundary: initialization PASS does not imply Vulkan rendering.
 * icd->info.rendering_status is always "blocked:ring-buffer-frame-proof-missing" until
 * a ring-buffer setup plus same-run frame proof is implemented.
 */
int uk_vulkan_icd_init(struct uk_vulkan_icd_dev *icd, uint32_t gpu_idx);

/*
 * Copy the device info snapshot populated by uk_vulkan_icd_init().
 * Safe to call after a successful init. Returns -EINVAL if icd is NULL
 * or not initialized.
 */
int uk_vulkan_icd_get_device_info(struct uk_vulkan_icd_dev *icd,
                                   struct uk_vulkan_icd_info *info_out);

/*
 * Tear down the ICD: destroys the Venus context and closes the vk.drm-shim device.
 */
void uk_vulkan_icd_close(struct uk_vulkan_icd_dev *icd);

/* Capset name helper (delegates to libukvirtio_gpu) */
const char *uk_vulkan_icd_capset_name(uint32_t id);

#endif /* UK_VULKAN_ICD_H */
