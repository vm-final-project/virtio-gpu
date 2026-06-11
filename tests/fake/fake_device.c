/* SPDX-License-Identifier: BSD-3-Clause */
#include "virtio_gpu_fake.h"

const struct uk_virtio_gpu_capset_info fake_capsets[MAX_CAPSETS] = {
	{ UK_VIRTIO_GPU_CAPSET_VIRGL, 2, 256 },
	{ UK_VIRTIO_GPU_CAPSET_VIRGL2, 2, 256 },
	{ UK_VIRTIO_GPU_CAPSET_VENUS, 1, 512 },
	{ UK_VIRTIO_GPU_CAPSET_DRM, 1, 128 },
	{ UK_VIRTIO_GPU_CAPSET_APIR, 1, 64 },
};

/* Core internal helpers */
struct fake_resource *find_res(struct uk_virtio_gpu_dev *dev, uk_gpu_res_id res)
{
	if (!dev || !res)
		return NULL;
	for (size_t i = 0; i < MAX_RESOURCES; i++)
		if (dev->resources[i].live && dev->resources[i].id == res)
			return &dev->resources[i];
	return NULL;
}

struct fake_resource *alloc_res(struct uk_virtio_gpu_dev *dev)
{
	if (!dev)
		return NULL;
	for (size_t i = 0; i < MAX_RESOURCES; i++) {
		if (!dev->resources[i].live) {
			memset(&dev->resources[i], 0, sizeof(dev->resources[i]));
			dev->resources[i].id = dev->next_res++;
			dev->resources[i].live = 1;
			dev->metrics.resources_created++;
			return &dev->resources[i];
		}
	}
	return NULL;
}

struct fake_context *find_ctx(struct uk_virtio_gpu_dev *dev, uk_gpu_ctx_id id)
{
	if (!dev || !id)
		return NULL;
	for (size_t i = 0; i < MAX_CONTEXTS; i++)
		if (dev->contexts[i].live && dev->contexts[i].id == id)
			return &dev->contexts[i];
	return NULL;
}

int capset_supported(uint32_t capset_id)
{
	for (size_t i = 0; i < MAX_CAPSETS; i++)
		if (fake_capsets[i].id == capset_id)
			return 1;
	return 0;
}

int checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
	if (!out)
		return -EINVAL;
	if (a && b > UINT64_MAX / a)
		return -EOVERFLOW;
	*out = a * b;
	return 0;
}

int resource_size(uint32_t width, uint32_t height, uint32_t depth, uint64_t *size)
{
	uint64_t wh;
	int rc;

	rc = checked_mul_u64(width, height, &wh);
	if (rc)
		return rc;
	rc = checked_mul_u64(wh, depth ? depth : 1u, &wh);
	if (rc)
		return rc;
	return checked_mul_u64(wh, 4u, size);
}

int complete_fence(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id *fence)
{
	if (!dev)
		return -EINVAL;
	uk_gpu_fence_id id = dev->next_fence++;
	dev->completed_fence = id;
	dev->metrics.fences_issued++;
	if (fence)
		*fence = id;
	return 0;
}

uint64_t rect_bytes(const struct uk_gpu_rect *r)
{
	return r ? (uint64_t)r->w * (uint64_t)r->h * 4u : 0;
}

/* Device Lifecycle APIs */
int uk_virtio_gpu_probe(struct uk_virtio_gpu_dev **dev)
{
	if (!dev)
		return -EINVAL;
	*dev = calloc(1, sizeof(**dev));
	if (!*dev)
		return -ENOMEM;
	(*dev)->next_res = 1;
	(*dev)->next_ctx = 1;
	(*dev)->next_fence = 1;
	(*dev)->metrics.probes = 1;
	return 0;
}

int uk_virtio_gpu_gl_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps)
{
	if (!dev || !caps)
		return -EINVAL;
	*caps = (struct uk_virtio_gpu_caps) {
		.num_scanouts = 1,
		.num_capsets = MAX_CAPSETS,
		.device_features = UK_VIRTIO_GPU_F_VIRGL | UK_VIRTIO_GPU_F_EDID |
			UK_VIRTIO_GPU_F_RESOURCE_UUID | UK_VIRTIO_GPU_F_RESOURCE_BLOB |
			UK_VIRTIO_GPU_F_CONTEXT_INIT,
		.negotiated_features = UK_VIRTIO_GPU_F_VIRGL | UK_VIRTIO_GPU_F_EDID |
			UK_VIRTIO_GPU_F_RESOURCE_UUID | UK_VIRTIO_GPU_F_RESOURCE_BLOB |
			UK_VIRTIO_GPU_F_CONTEXT_INIT,
		.has_virgl = 1,
		.has_edid = 1,
		.has_resource_uuid = 1,
		.has_resource_blob = 1,
		.has_context_init = 1,
		.uses_blob_scanout = 0,
		.has_host_visible = 1,
	};
	return 0;
}

int uk_virtio_gpu_gl_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	if (!dev || !info)
		return -EINVAL;
	if (index >= MAX_CAPSETS)
		return -ENOENT;
	*info = fake_capsets[index];
	return 0;
}

int uk_virtio_gpu_gl_capset_get(struct uk_virtio_gpu_dev *dev, uint32_t capset_id, uint32_t version, void *buf, size_t len, size_t *actual_len)
{
	const char *payload = "unikraft-fake-virgl-capset";
	size_t need = strlen(payload) + 1;
	if (!dev || !capset_supported(capset_id) || version == 0)
		return -EINVAL;
	if (actual_len)
		*actual_len = need;
	if (!buf || len < need)
		return -ENOSPC;
	memcpy(buf, payload, need);
	return 0;
}

int uk_virtio_gpu_gl_get_edid(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, void *buf, size_t len, size_t *actual_len)
{
	uint8_t edid[128];
	if (!dev || scanout_id != 0)
		return -EINVAL;
	memset(edid, 0, sizeof(edid));
	edid[0] = 0x00; edid[1] = 0xff; edid[2] = 0xff; edid[3] = 0xff;
	edid[4] = 0xff; edid[5] = 0xff; edid[6] = 0xff; edid[7] = 0x00;
	if (actual_len)
		*actual_len = sizeof(edid);
	if (!buf || len < sizeof(edid))
		return -ENOSPC;
	memcpy(buf, edid, sizeof(edid));
	return 0;
}

int uk_virtio_gpu_gl_set_scanout(struct uk_virtio_gpu_dev *dev, uint32_t scanout_id, uk_gpu_res_id res, const struct uk_gpu_rect *r)
{
	if (!find_res(dev, res) || scanout_id != 0 || !r || !r->w || !r->h)
		return -EINVAL;
	return 0;
}

int uk_virtio_gpu_fence_wait(struct uk_virtio_gpu_dev *dev, uk_gpu_fence_id fence, uint64_t timeout_ns)
{
	(void)timeout_ns;
	if (!dev || !fence)
		return -EINVAL;
	dev->metrics.fence_waits++;
	return fence <= dev->completed_fence ? 0 : -ETIMEDOUT;
}

/* Weak Stubs / Driver Entrypoint implementations */
struct uk_virtio_gpu_dev *WEAK uk_virtio_gpu_default_dev(void)
{
	return NULL;
}

struct uk_virtio_gpu_dev *WEAK uk_virtio_gpu_from_fbdev(struct uk_fbdev *fbdev)
{
	(void)fbdev;
	return NULL;
}

int WEAK uk_virtio_gpu_dev_caps_get(struct uk_virtio_gpu_dev *dev, struct uk_virtio_gpu_caps *caps)
{
	return uk_virtio_gpu_gl_caps_get(dev, caps);
}

int WEAK uk_virtio_gpu_dev_capset_info_get(struct uk_virtio_gpu_dev *dev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	return uk_virtio_gpu_gl_capset_info_get(dev, index, info);
}

int WEAK uk_virtio_gpu_dev_shm_region_get(struct uk_virtio_gpu_dev *dev, uint32_t id, struct uk_virtio_gpu_shm_region *region)
{
	(void)dev; (void)id; (void)region;
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_caps_get(struct uk_fbdev *fbdev, struct uk_virtio_gpu_caps *caps)
{
	return uk_virtio_gpu_dev_caps_get(uk_virtio_gpu_from_fbdev(fbdev), caps);
}

int WEAK uk_virtio_gpu_capset_info_get(struct uk_fbdev *fbdev, uint32_t index, struct uk_virtio_gpu_capset_info *info)
{
	return uk_virtio_gpu_dev_capset_info_get(uk_virtio_gpu_from_fbdev(fbdev), index, info);
}

const char *WEAK uk_virtio_gpu_capset_name(uint32_t capset_id)
{
	return uk_virtio_gpu_gl_capset_name(capset_id);
}

const char *uk_virtio_gpu_gl_capset_name(uint32_t capset_id)
{
	switch (capset_id) {
	case UK_VIRTIO_GPU_CAPSET_VIRGL: return "virgl";
	case UK_VIRTIO_GPU_CAPSET_VIRGL2: return "virgl2";
	case UK_VIRTIO_GPU_CAPSET_GFXSTREAM: return "gfxstream";
	case UK_VIRTIO_GPU_CAPSET_VENUS: return "venus";
	case UK_VIRTIO_GPU_CAPSET_CROSS_DOMAIN: return "cross-domain";
	case UK_VIRTIO_GPU_CAPSET_DRM: return "drm-native-context";
	case UK_VIRTIO_GPU_CAPSET_APIR: return "apir";
	default: return "unknown";
	}
}

/* APIR weak stubs */
static int apir_put_u32(uint8_t *buf, size_t len, size_t *off, uint32_t value)
{
	if (!buf || !off || *off > len || len - *off < sizeof(value))
		return -ENOSPC;
	memcpy(buf + *off, &value, sizeof(value));
	*off += sizeof(value);
	return 0;
}

static int apir_get_u32(const uint8_t *buf, size_t len, size_t *off, uint32_t *value)
{
	if (!buf || !off || !value || *off > len || len - *off < sizeof(*value))
		return -EINVAL;
	memcpy(value, buf + *off, sizeof(*value));
	*off += sizeof(*value);
	return 0;
}

int WEAK uk_virtio_gpu_apir_encode_header(void *buf, size_t len, const struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0;
	int rc;
	if (!buf || !msg || msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH)
		return -EINVAL;
	rc = apir_put_u32(buf, len, &off, msg->command_type);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->flags);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->reply_resource_id);
	if (rc) return rc;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_decode_header(const void *buf, size_t len, struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0;
	int rc;
	if (!buf || !msg)
		return -EINVAL;
	rc = apir_get_u32(buf, len, &off, &msg->command_type);
	if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->flags);
	if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->reply_resource_id);
	if (rc) return rc;
	if (msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH)
		return -EINVAL;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_encode_handshake(void *buf, size_t len, uint32_t reply_resource_id, size_t *used)
{
	struct uk_virtio_gpu_apir_msg msg = {
		.command_type = UK_VIRTIO_GPU_APIR_COMMAND_HANDSHAKE,
		.flags = 0,
		.reply_resource_id = reply_resource_id,
	};
	size_t off = 0;
	int rc = uk_virtio_gpu_apir_encode_header(buf, len, &msg, &off);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR);
	if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR);
	if (rc) return rc;
	if (used) *used = off;
	return 0;
}

int WEAK uk_virtio_gpu_apir_decode_handshake_reply(const void *buf, size_t len, uint32_t host_rc, struct uk_virtio_gpu_apir_handshake *reply)
{
	size_t off = 0;
	if (!reply || host_rc != UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC)
		return -EINVAL;
	reply->guest_major = UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR;
	reply->guest_minor = UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR;
	if (apir_get_u32(buf, len, &off, &reply->host_major))
		return -EINVAL;
	if (apir_get_u32(buf, len, &off, &reply->host_minor))
		return -EINVAL;
	return 0;
}

int WEAK uk_virtio_gpu_apir_handshake(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_handshake *reply, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob; (void)reply;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_apir_load_library(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}

int WEAK uk_virtio_gpu_apir_forward(struct uk_virtio_gpu_dev *dev, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *reply_blob, uint32_t flags, const void *payload, size_t payload_len, struct uk_virtio_gpu_apir_status *status)
{
	(void)dev; (void)ctx; (void)reply_blob; (void)flags; (void)payload; (void)payload_len;
	if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP, .host_rc = (uint32_t)-1, .reply_size = 0 };
	return -ENOTSUP;
}
