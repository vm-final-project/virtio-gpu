/* SPDX-License-Identifier: MIT */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/vulkan/vn_renderer_virtgpu.c renderer init path.
 *   Same: open device, query required params, decode Venus capset, create context.
 *   VOGUE adaptation: native libukvirtio_gpu transport instead of DRM ioctls.
 */
#include <errno.h>
#include <string.h>

#include <uk/vn_renderer.h>

static int renderer_fill_capsets(struct uk_venus_renderer *r,
				 const struct uk_virtio_gpu_caps *caps)
{
	struct uk_virtio_gpu_capset_info ci;
	int rc;

	for (uint32_t i = 0; i < caps->num_capsets && i < 64u; i++) {
		rc = uk_virtio_gpu_gl_capset_info_get(r->gpu, i, &ci);
		if (rc)
			return rc;
		if (ci.id < 64u)
			r->info.supported_capsets |= (1ull << ci.id);
	}

	if (!(r->info.supported_capsets & (1ull << UK_VIRTIO_GPU_CAPSET_VENUS))) {
		r->info.status = "blocked:capset-not-advertised";
		return -ENOTSUP;
	}

	return 0;
}

static int renderer_fill_venus_caps(struct uk_venus_renderer *r)
{
	struct uk_venus_caps vcaps;
	int rc;

	rc = uk_venus_capset_get(r->gpu, &vcaps);
	if (rc) {
		r->info.status = "blocked:capset-query-failed";
		return rc;
	}

	r->info.wire_format_version = vcaps.wire_format_version;
	r->info.vk_xml_version = vcaps.vk_xml_version;
	r->info.vk_ext_command_serialization_spec_version =
		vcaps.vk_ext_command_serialization_spec_version;
	r->info.vk_mesa_venus_protocol_spec_version =
		vcaps.vk_mesa_venus_protocol_spec_version;
	r->info.supports_blob_id_0 = vcaps.supports_blob_id_0;
	memcpy(r->info.vk_extension_mask, vcaps.vk_extension_mask1,
	       sizeof(r->info.vk_extension_mask));
	r->info.allow_vk_wait_syncs = vcaps.allow_vk_wait_syncs;
	r->info.supports_multiple_timelines = vcaps.supports_multiple_timelines;
	r->info.use_guest_vram = vcaps.use_guest_vram;
	r->info.has_guest_vram = vcaps.use_guest_vram ? 1 : 0;

	if (!r->info.wire_format_version) {
		r->info.status = "blocked:invalid-wire-format-version";
		return -ENOTSUP;
	}

	return 0;
}

int uk_venus_renderer_open(struct uk_venus_renderer *r,
			   uint32_t gpu_idx,
			   enum uk_venus_open_mode mode)
{
	struct uk_virtio_gpu_caps caps;
	int rc;

	(void)gpu_idx;
	if (!r)
		return -EINVAL;
	memset(r, 0, sizeof(*r));
	r->mode = mode;

	rc = uk_virtio_gpu_probe(&r->gpu);
	if (rc) {
		r->info.status = "blocked:no-device";
		return rc;
	}

	rc = uk_virtio_gpu_gl_caps_get(r->gpu, &caps);
	if (rc) {
		r->info.status = "blocked:caps-query-failed";
		return rc;
	}

	r->info.has_resource_blob = caps.has_resource_blob;
	r->info.has_host_visible = caps.has_host_visible;
	r->info.has_context_init = caps.has_context_init;

	rc = renderer_fill_capsets(r, &caps);
	if (rc)
		goto maybe_probe_ok;

	rc = renderer_fill_venus_caps(r);
	if (rc)
		goto maybe_probe_ok;

	if (!caps.has_resource_blob) {
		rc = -ENOTSUP;
		r->info.status = "blocked:resource-blob-missing";
		goto maybe_probe_ok;
	}
	if (!caps.has_host_visible) {
		rc = -ENOTSUP;
		r->info.status = "blocked:host-visible-missing";
		goto maybe_probe_ok;
	}
	if (!caps.has_context_init) {
		rc = -ENOTSUP;
		r->info.status = "blocked:context-init-missing";
		goto maybe_probe_ok;
	}

	rc = uk_virtio_gpu_gl_context_create(r->gpu,
					     UK_VIRTIO_GPU_CAPSET_VENUS,
					     "uk-venus", &r->ctx);
	if (rc) {
		r->info.status = "blocked:context-create-failed";
		goto maybe_probe_ok;
	}

	r->info.context_ready = 1;
	r->info.status = "pass";
	r->opened = 1;
	return 0;

maybe_probe_ok:
	if (mode == UK_VENUS_OPEN_PROBE) {
		r->opened = 1;
		return 0;
	}
	uk_venus_renderer_close(r);
	return rc ? rc : -ENOTSUP;
}

void uk_venus_renderer_close(struct uk_venus_renderer *r)
{
	if (!r)
		return;
	if (r->info.context_ready && r->gpu)
		uk_virtio_gpu_gl_context_destroy(r->gpu, &r->ctx);
	memset(r, 0, sizeof(*r));
}
