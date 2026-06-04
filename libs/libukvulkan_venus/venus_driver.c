/* SPDX-License-Identifier: MIT */
/*
 * venus_driver.c — Unikraft-native Venus Vulkan driver open/probe bootstrap.
 *
 * This is the driver-open path declared in <uk/vulkan_venus.h>, consumed by
 * libvulkan and the Vulkan benchmark/smoke ports. It owns the device-open /
 * Venus-context bootstrap that previously lived in the libukvk_icd shim, but
 * does it natively through libukvirtio_gpu (no Linux virtgpu DRM UAPI):
 *
 *   uk_virtio_gpu_probe()            -> native VirtIO-GPU device
 *   uk_virtio_gpu_gl_caps_get()      -> blob / host-visible / context-init caps
 *   uk_virtio_gpu_gl_capset_info_*() -> supported capset bitmask (Venus = id 4)
 *   uk_virtio_gpu_gl_context_create()-> Venus rendering context
 *
 * The heavy Venus command encode/decode + transport policy live in the other
 * driver TUs (venus_init.c / venus_cs.c / venus_compute.c / vn_ring_shim.c).
 */
#include <uk/vulkan_venus.h>
#include <uk/venus.h>
#include <uk/virtio_gpu.h>
#include <string.h>
#include <errno.h>

int uk_vulkan_venus_open(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx)
{
	struct uk_virtio_gpu_caps caps;
	struct uk_virtio_gpu_capset_info ci;
	int rc;

	(void)gpu_idx;
	if (!dev)
		return -EINVAL;
	memset(dev, 0, sizeof(*dev));

	/* Native VirtIO-GPU device open (no DRM UAPI). */
	rc = uk_virtio_gpu_probe(&dev->gpu);
	if (rc)
		return rc;

	rc = uk_virtio_gpu_gl_caps_get(dev->gpu, &caps);
	if (rc)
		return rc;

	dev->info.capset_id        = UK_VIRTIO_GPU_CAPSET_VENUS;
	dev->info.has_resource_blob = caps.has_resource_blob;
	dev->info.has_host_visible  = caps.has_host_visible;
	dev->info.has_context_init  = caps.has_context_init;

	/* Build the supported-capset bitmask and detect Venus (capset id 4). */
	for (uint32_t i = 0; i < caps.num_capsets && i < 64u; i++) {
		if (uk_virtio_gpu_gl_capset_info_get(dev->gpu, i, &ci) == 0
		    && ci.id < 64u)
			dev->info.supported_capsets |= (1ull << ci.id);
	}
	dev->info.venus_available =
		(dev->info.supported_capsets & (1ull << UK_VIRTIO_GPU_CAPSET_VENUS))
			? 1 : 0;

	strncpy(dev->info.device_name, "virtio-gpu-venus",
		UK_VULKAN_VENUS_MAX_NAME - 1);
	dev->info.device_name[UK_VULKAN_VENUS_MAX_NAME - 1] = '\0';

	/* Create the Venus rendering context (capset id 4). Non-fatal on fake
	 * backends that expose the capset but reject the context create. */
	if (dev->info.has_context_init && dev->info.venus_available) {
		rc = uk_virtio_gpu_gl_context_create(dev->gpu,
						     UK_VIRTIO_GPU_CAPSET_VENUS,
						     "uk-venus", &dev->ctx);
		if (rc == 0)
			dev->ctx_ready = 1;
	}

	/* Claim boundary: the driver substrate is up; full Venus rendering still
	 * depends on the command encoders + same-run frame proof. */
	dev->info.rendering_status = "blocked:ring-buffer-frame-proof-missing";
	dev->opened = 1;
	return 0;
}

void uk_vulkan_venus_close(struct uk_vulkan_venus_dev *dev)
{
	if (!dev)
		return;
	if (dev->ctx_ready && dev->gpu)
		uk_virtio_gpu_gl_context_destroy(dev->gpu, &dev->ctx);
	memset(dev, 0, sizeof(*dev));
}

int uk_vulkan_venus_get_info(struct uk_vulkan_venus_dev *dev,
			     struct uk_vulkan_venus_info *out)
{
	if (!dev || !out)
		return -EINVAL;
	if (!dev->opened)
		return -EINVAL;
	*out = dev->info;
	return 0;
}

const char *uk_vulkan_venus_probe_status(struct uk_vulkan_venus_dev *dev)
{
	if (!dev || !dev->opened)
		return "venus driver uninitialised (libukvulkan_venus)";
	if (dev->ctx_ready)
		return "venus driver ready (libukvulkan_venus); native VirtIO-GPU context";
	if (dev->info.venus_available)
		return "venus capset present; context not created (libukvulkan_venus)";
	return "venus capset unavailable (libukvulkan_venus)";
}

struct uk_virtio_gpu_dev *uk_vulkan_venus_gpu(struct uk_vulkan_venus_dev *dev)
{
	return dev ? dev->gpu : (struct uk_virtio_gpu_dev *)0;
}

struct uk_virtio_gpu_context *uk_vulkan_venus_ctx(struct uk_vulkan_venus_dev *dev)
{
	return dev ? &dev->ctx : (struct uk_virtio_gpu_context *)0;
}
