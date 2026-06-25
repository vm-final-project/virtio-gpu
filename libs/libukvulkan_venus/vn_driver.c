/* SPDX-License-Identifier: MIT */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/vulkan/vn_renderer_virtgpu.c init entry path.
 *   Same: probe VirtIO-GPU, inspect caps/capsets, create Venus context.
 *   VOGUE adaptation: compatibility API over native libukvirtio_gpu, no DRM UAPI.
 */
/*
 * vn_driver.c — Unikraft-native Venus Vulkan driver open/probe bootstrap.
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
 * driver TUs (vn_ring.c, vn_cs.c, vn_compute.c, vn_ring_shim.c).
 */
#include <uk/vulkan_venus.h>
#include <uk/venus.h>
#include <uk/vn_renderer.h>
#include <uk/virtio_gpu.h>
#include <string.h>
#include <errno.h>

static void venus_info_from_renderer(struct uk_vulkan_venus_dev *dev,
				     const struct uk_venus_renderer *r)
{
	dev->gpu = r->gpu;
	dev->ctx = r->ctx;
	dev->ctx_ready = r->info.context_ready;
	dev->info.capset_id = UK_VIRTIO_GPU_CAPSET_VENUS;
	dev->info.has_resource_blob = r->info.has_resource_blob;
	dev->info.has_host_visible = r->info.has_host_visible;
	dev->info.has_context_init = r->info.has_context_init;
	dev->info.supported_capsets = r->info.supported_capsets;
	dev->info.venus_available =
		(r->info.supported_capsets & (1ull << UK_VIRTIO_GPU_CAPSET_VENUS))
			? 1 : 0;
	strncpy(dev->info.device_name, "virtio-gpu-venus",
		UK_VULKAN_VENUS_MAX_NAME - 1);
	dev->info.device_name[UK_VULKAN_VENUS_MAX_NAME - 1] = '\0';
	dev->info.rendering_status =
		r->info.status ? r->info.status : "blocked:venus-open-unknown";
	dev->opened = 1;
}

static int venus_open_mode(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx,
			   enum uk_venus_open_mode mode)
{
	struct uk_venus_renderer renderer;
	int rc;

	if (!dev)
		return -EINVAL;
	memset(dev, 0, sizeof(*dev));

	rc = uk_venus_renderer_open(&renderer, gpu_idx, mode);
	if (rc)
		return rc;
	venus_info_from_renderer(dev, &renderer);

	return 0;
}

int uk_vulkan_venus_open(struct uk_vulkan_venus_dev *dev, uint32_t gpu_idx)
{
	return venus_open_mode(dev, gpu_idx, UK_VENUS_OPEN_PROBE);
}

int uk_vulkan_venus_open_strict(struct uk_vulkan_venus_dev *dev,
				uint32_t gpu_idx)
{
	return venus_open_mode(dev, gpu_idx, UK_VENUS_OPEN_STRICT);
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
