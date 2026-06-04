/* SPDX-License-Identifier: MIT */
/*
 * libukvulkan_venus — Unikraft-native Venus Vulkan driver implementation.
 *
 * This header is the driver-facing surface consumed by libvulkan, the Vulkan
 * benchmark/smoke ports, and tests. It exposes the Venus *driver* open/probe
 * handle, NOT the public vk* ABI (which libvulkan owns).
 *
 * The driver-open path is native: it probes the VirtIO-GPU device through
 * libukvirtio_gpu directly (no Linux virtgpu DRM UAPI), queries the Venus
 * capset, and creates a Venus rendering context. It supersedes the former
 * libukvk_icd bootstrap shim, whose ownership now lives here.
 */
#pragma once
#include <stdint.h>
#include <uk/virtio_gpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UK_VULKAN_VENUS_MAX_NAME 64

/* Device capability snapshot, filled by uk_vulkan_venus_open(). */
struct uk_vulkan_venus_info {
	uint32_t capset_id;           /* UK_VIRTIO_GPU_CAPSET_VENUS (4) */
	int      has_resource_blob;
	int      has_host_visible;
	int      has_context_init;
	uint64_t supported_capsets;   /* bitmask of enumerated capset ids */
	int      venus_available;     /* Venus capset (id 4) present */
	char     device_name[UK_VULKAN_VENUS_MAX_NAME];
	const char *rendering_status; /* claim-boundary marker */
};

/*
 * Venus driver device. Holds the native VirtIO-GPU device handle and the Venus
 * rendering context. Callers allocate this (e.g. statically); the layer above
 * (libvulkan) reads the gpu/ctx via the accessors below.
 */
struct uk_vulkan_venus_dev {
	struct uk_virtio_gpu_dev     *gpu;   /* native VirtIO-GPU device */
	struct uk_virtio_gpu_context  ctx;   /* Venus context (capset id 4) */
	struct uk_vulkan_venus_info   info;
	int                           ctx_ready;
	int                           opened;
};

/*
 * Open / close the Venus Vulkan driver for the given GPU index.
 * uk_vulkan_venus_open() probes the VirtIO-GPU device via libukvirtio_gpu,
 * queries the Venus capset, fills `info`, and (when supported) creates a Venus
 * context. Returns 0 on success, negative on failure.
 */
int  uk_vulkan_venus_open(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx);
void uk_vulkan_venus_close(struct uk_vulkan_venus_dev *dev);

/* Copy the device capability snapshot. Returns 0 on success. */
int  uk_vulkan_venus_get_info(struct uk_vulkan_venus_dev *dev,
			      struct uk_vulkan_venus_info *out);

/* Human-readable probe/diagnostic status, reported as backend_name="venus". */
const char *uk_vulkan_venus_probe_status(struct uk_vulkan_venus_dev *dev);

/* Accessors for the layer above (libvulkan dispatch). */
struct uk_virtio_gpu_dev     *uk_vulkan_venus_gpu(struct uk_vulkan_venus_dev *dev);
struct uk_virtio_gpu_context *uk_vulkan_venus_ctx(struct uk_vulkan_venus_dev *dev);

#ifdef __cplusplus
}
#endif
