#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <uk/alloc.h>
#include <uk/print.h>
#include <uk/sglist.h>
#include <uk/mutex.h>
#include <uk/plat/time.h>
#include <virtio/virtio_bus.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtqueue.h>

#include "virtio_gpu_real_priv.h"

UKVGPU_STATIC_ASSERTS();

#define DRIVER_NAME "libukvirtio_gpu"
#define CMD_TIMEOUT_NS 1000000000ull
#define MAX_CMD_SEGS 4u
#define UKVGPU_SHM_ID_HOST_VISIBLE 1u

static struct uk_alloc *g_alloc;
static struct uk_virtio_gpu_dev *g_default_dev;

/* Provided by the modern virtio-pci transport (unikraft drivers/virtio/pci):
 * resolves a virtio shared-memory PCI capability (cfg_type 8) to a mapped
 * host-visible window. Declared extern (not weak) because Unikraft's two-stage
 * partial linking (ld -r) pre-binds a same-TU weak definition to the local
 * caller, which prevents the transport's strong symbol from overriding it. */
extern int virtio_pci_shm_region_get(struct virtio_dev *vdev, uint8_t id,
				     void **base, uint64_t *len);

static uint64_t now_ns(void)
{
	return (uint64_t)ukplat_monotonic_clock();
}

static uint64_t resource_size_bytes(uint32_t w, uint32_t h, uint32_t d)
{
	uint64_t wh = (uint64_t)w * (uint64_t)h;
	return wh * (uint64_t)(d ? d : 1u) * 4ull;
}

static uint32_t compat_format(uint32_t fmt)
{
	return fmt == 1u ? UKVGPU_FORMAT_B8G8R8X8_UNORM : fmt;
}

static struct ukvgpu_resource_state *find_res(struct uk_virtio_gpu_dev *d,
						      uk_gpu_res_id id)
{
	if (!d || !id)
		return NULL;
	for (size_t i = 0; i < UKVGPU_MAX_RESOURCES; i++)
		if (d->resources[i].live && d->resources[i].id == id)
			return &d->resources[i];
	return NULL;
}

static struct ukvgpu_resource_state *alloc_res(struct uk_virtio_gpu_dev *d)
{
	for (size_t i = 0; d && i < UKVGPU_MAX_RESOURCES; i++) {
		if (!d->resources[i].live) {
			memset(&d->resources[i], 0, sizeof(d->resources[i]));
			d->resources[i].id = d->next_res++;
			d->resources[i].live = 1;
			d->metrics.resources_created++;
			return &d->resources[i];
		}
	}
	return NULL;
}

static int gpu_resp_errno(uint32_t type)
{
	switch (type) {
	case UKVGPU_RESP_OK_NODATA:
	case UKVGPU_RESP_OK_DISPLAY_INFO:
	case UKVGPU_RESP_OK_CAPSET_INFO:
	case UKVGPU_RESP_OK_CAPSET:
	case UKVGPU_RESP_OK_EDID:
	case UKVGPU_RESP_OK_RESOURCE_UUID:
	case UKVGPU_RESP_OK_MAP_INFO:
		return 0;
	case UKVGPU_RESP_ERR_OUT_OF_MEMORY:
		return -ENOMEM;
	case UKVGPU_RESP_ERR_INVALID_SCANOUT_ID:
	case UKVGPU_RESP_ERR_INVALID_RESOURCE_ID:
	case UKVGPU_RESP_ERR_INVALID_CONTEXT_ID:
	case UKVGPU_RESP_ERR_INVALID_PARAMETER:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static void hdr_init(struct uk_virtio_gpu_dev *d, struct ukvgpu_ctrl_hdr *h,
		     uint32_t type, uk_gpu_fence_id *fence, uint32_t ctx_id)
{
	memset(h, 0, sizeof(*h));
	h->type = type;
	h->ctx_id = ctx_id;
	if (fence) {
		*fence = d->next_fence++;
		h->flags = UKVGPU_FLAG_FENCE;
		h->fence_id = *fence;
	}
}

/*
 * Serialise all control-virtqueue traffic. The Linux virtio-gpu driver wraps
 * every virtqueue_add_sgs()+kick in vgdev->ctrlq.qlock (see
 * drivers/gpu/drm/virtio/virtgpu_vq.c). We have the same requirement: the
 * guest-side Vulkan dispatch issues Venus SUBMIT_3D / blob-map / fence commands
 * from ggml-vulkan's concurrent pipeline-compile worker threads, and a shared
 * single control queue cannot be driven by two threads at once (enqueue cookie,
 * notify, and the busy-poll dequeue all race). A sleeping uk_mutex is correct
 * here because cmd_submit() polls (and the scheduler may switch threads) while
 * waiting for the host response. Recursive so a future nested submit is safe.
 */
static struct uk_mutex g_ctrlq_lock =
	UK_MUTEX_INITIALIZER_RECURSIVE(g_ctrlq_lock);

static int cmd_submit_locked(struct uk_virtio_gpu_dev *d, void *req, size_t req_len,
		      void *resp, size_t resp_len, uint32_t expect,
		      uk_gpu_fence_id *fence)
{
	struct uk_sglist sg;
	struct uk_sglist_seg segs[MAX_CMD_SEGS];
	void *cookie = req;
	void *done = NULL;
	uint32_t cmd_type = ((struct ukvgpu_ctrl_hdr *)req)->type;
	uint32_t used_len = 0;
	uint64_t deadline;
	int rc;

	if (!d || !d->ctrlq || !req || !req_len || !resp || !resp_len)
		return -EINVAL;

	uk_sglist_init(&sg, MAX_CMD_SEGS, segs);
	rc = uk_sglist_append(&sg, req, req_len);
	if (rc)
		return rc;
	rc = uk_sglist_append(&sg, resp, resp_len);
	if (rc)
		return rc;

	/* read_bufs=1: device reads command; write_bufs=1: device writes response. */
	rc = virtqueue_buffer_enqueue(d->ctrlq, cookie, &sg, 1, 1);
	if (rc < 0)
		return rc;
	virtqueue_host_notify(d->ctrlq);

	deadline = now_ns() + CMD_TIMEOUT_NS;
	for (;;) {
		rc = virtqueue_buffer_dequeue(d->ctrlq, &done, &used_len);
		if (rc >= 0)
			break;
		if (rc != -ENOMSG)
			return rc;
		if (now_ns() > deadline)
			return -ETIMEDOUT;
	}
	if (done != cookie)
		return -EIO;
	if (used_len < sizeof(struct ukvgpu_ctrl_hdr))
		return -EIO;

	struct ukvgpu_ctrl_hdr *rh = (struct ukvgpu_ctrl_hdr *)resp;
	if (rh->type == 0 &&
	    (cmd_type == UKVGPU_CMD_CTX_CREATE ||
	     cmd_type == UKVGPU_CMD_CTX_DESTROY)) {
		/*
		 * QEMU/virgl completes Venus context create/destroy with a used
		 * response descriptor but leaves the nodata header zeroed on
		 * this local stack (observed same-run with QEMU 11.0 + Venus).
		 * The command has been accepted by virtio-gpu at this point; a
		 * later Venus register/blob command is the real host proof.
		 */
		return 0;
	}
	rc = gpu_resp_errno(rh->type);
	if (rc) {
		printf("uk-vgpu: cmd 0x%x resp 0x%x rc=%d used=%u expect=0x%x\n",
		       cmd_type, rh->type, rc, used_len, expect);
		return rc;
	}
	if (expect && rh->type != expect) {
		printf("uk-vgpu: cmd 0x%x unexpected resp 0x%x used=%u expect=0x%x\n",
		       cmd_type, rh->type, used_len, expect);
		return -EIO;
	}
	if (fence && (rh->flags & UKVGPU_FLAG_FENCE) && rh->fence_id == *fence)
		d->completed_fence = *fence;
	return 0;
}

/* Lock wrapper around the control queue (mirrors Linux ctrlq.qlock). */
static int cmd_submit(struct uk_virtio_gpu_dev *d, void *req, size_t req_len,
		      void *resp, size_t resp_len, uint32_t expect,
		      uk_gpu_fence_id *fence)
{
	int rc;

	uk_mutex_lock(&g_ctrlq_lock);
	rc = cmd_submit_locked(d, req, req_len, resp, resp_len, expect, fence);
	uk_mutex_unlock(&g_ctrlq_lock);
	return rc;
}

int uk_virtio_gpu_get_display_info(struct uk_virtio_gpu_dev *d)
{
	struct ukvgpu_ctrl_hdr req;
	struct ukvgpu_resp_display_info resp;
	int rc;

	if (!d)
		return -EINVAL;
	hdr_init(d, &req, UKVGPU_CMD_GET_DISPLAY_INFO, NULL, 0);
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_DISPLAY_INFO, NULL);
	if (rc)
		return rc;
	memcpy(d->scanouts, resp.pmodes, sizeof(d->scanouts));
	for (uint32_t i = 0; i < d->num_scanouts && i < UKVGPU_MAX_SCANOUTS; i++) {
		uk_pr_info(DRIVER_NAME": scanout %u enabled=%u rect=%ux%u+%u+%u\n",
			   i, d->scanouts[i].enabled, d->scanouts[i].r.width,
			   d->scanouts[i].r.height, d->scanouts[i].r.x,
			   d->scanouts[i].r.y);
	}
	return 0;
}

int uk_virtio_gpu_resource_create_2d(struct uk_virtio_gpu_dev *d, uint32_t w,
				     uint32_t h, uint32_t fmt, uk_gpu_res_id *out)
{
	struct ukvgpu_resource_create_2d req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r;
	int rc;

	if (!d || !w || !h || !out)
		return -EINVAL;
	r = alloc_res(d);
	if (!r)
		return -ENOMEM;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_CREATE_2D, NULL, 0);
	req.resource_id = r->id;
	req.format = compat_format(fmt);
	req.width = w;
	req.height = h;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (rc) {
		memset(r, 0, sizeof(*r));
		return rc;
	}
	r->width = w; r->height = h; r->depth = 1; r->format = req.format;
	r->size = resource_size_bytes(w, h, 1);
	*out = r->id;
	return 0;
}

int uk_virtio_gpu_resource_attach_backing(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, const struct uk_sglist *sg)
{
	struct {
		struct ukvgpu_resource_attach_backing req;
		struct ukvgpu_mem_entry ents[16];
	} cmd;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = find_res(d, res);
	uint16_t nr_sg;
	int rc;

	if (!r || !sg || !sg->sg_nseg || sg->sg_nseg > 16)
		return -EINVAL;
	nr_sg = sg->sg_nseg;
	memset(&cmd, 0, sizeof(cmd));
	hdr_init(d, &cmd.req.hdr, UKVGPU_CMD_RESOURCE_ATTACH_BACKING, NULL, 0);
	cmd.req.resource_id = res;
	cmd.req.nr_entries = (uint32_t)nr_sg;
	for (uint16_t i = 0; i < nr_sg; i++) {
		if (!sg->sg_segs[i].ss_len)
			return -EINVAL;
		cmd.ents[i].addr = (uint64_t)sg->sg_segs[i].ss_paddr;
		cmd.ents[i].length = (uint32_t)sg->sg_segs[i].ss_len;
	}
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &cmd, sizeof(cmd.req) + (size_t)nr_sg * sizeof(cmd.ents[0]),
			&resp, sizeof(resp), UKVGPU_RESP_OK_NODATA, NULL);
	if (!rc) {
		r->backing_attached = 1;
		d->metrics.attach_calls++;
	}
	return rc;
}

int uk_virtio_gpu_transfer_to_host_2d(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, const struct uk_gpu_rect *rect,
		uk_gpu_fence_id *fence)
{
	struct ukvgpu_transfer_to_host_2d req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = find_res(d, res);
	int rc;
	if (!r || !rect || !rect->w || !rect->h || !r->backing_attached)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_TRANSFER_TO_HOST_2D, fence, 0);
	req.r = (struct ukvgpu_rect){ rect->x, rect->y, rect->w, rect->h };
	req.offset = 0;
	req.resource_id = res;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, fence);
	if (!rc) {
		d->metrics.transfers_to_host++;
		d->metrics.bytes_to_host += (uint64_t)rect->w * rect->h * 4u;
	}
	return rc;
}

int uk_virtio_gpu_resource_flush(struct uk_virtio_gpu_dev *d, uk_gpu_res_id res,
		const struct uk_gpu_rect *rect, uk_gpu_fence_id *fence)
{
	struct ukvgpu_resource_flush req;
	struct ukvgpu_ctrl_hdr resp;
	if (!find_res(d, res) || !rect || !rect->w || !rect->h)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_FLUSH, fence, 0);
	req.r = (struct ukvgpu_rect){ rect->x, rect->y, rect->w, rect->h };
	req.resource_id = res;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	int rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, fence);
	if (!rc)
		d->metrics.flushes++;
	return rc;
}

/*
 * Coalesce TRANSFER_TO_HOST_2D + RESOURCE_FLUSH into a single fenced batch.
 *
 * Per VirtIO-GPU spec, commands without VIRTIO_GPU_FLAG_FENCE complete in
 * issue order. The transfer is submitted with NO fence; the flush carries the
 * single output fence. By the time the flush fence retires, the transfer is
 * guaranteed complete on the host side. Callers spend ONE `fence_wait` per
 * frame instead of two.
 */
int uk_virtio_gpu_transfer_and_flush_2d(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, const struct uk_gpu_rect *rect,
		uk_gpu_fence_id *fence)
{
	int rc;
	if (!fence)
		return -EINVAL;
	rc = uk_virtio_gpu_transfer_to_host_2d(d, res, rect, NULL);
	if (rc)
		return rc;
	return uk_virtio_gpu_resource_flush(d, res, rect, fence);
}

int uk_virtio_gpu_fence_wait(struct uk_virtio_gpu_dev *d, uk_gpu_fence_id fence,
		uint64_t timeout_ns)
{
	(void)timeout_ns;
	if (!d || !fence)
		return -EINVAL;
	d->metrics.fence_waits++;
	return fence <= d->completed_fence ? 0 : -ETIMEDOUT;
}

int uk_virtio_gpu_gl_set_scanout(struct uk_virtio_gpu_dev *d, uint32_t scanout,
		uk_gpu_res_id res, const struct uk_gpu_rect *rect)
{
	struct ukvgpu_set_scanout req;
	struct ukvgpu_ctrl_hdr resp;
	if (!d || scanout >= d->num_scanouts || !find_res(d, res) || !rect)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_SET_SCANOUT, NULL, 0);
	req.r = (struct ukvgpu_rect){ rect->x, rect->y, rect->w, rect->h };
	req.scanout_id = scanout;
	req.resource_id = res;
	memset(&resp, 0, sizeof(resp));
	return cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
}

int uk_virtio_gpu_gl_resource_detach_backing(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res)
{
	struct ukvgpu_resource_detach_backing req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = find_res(d, res);
	if (!r || !r->backing_attached)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_DETACH_BACKING, NULL, 0);
	req.resource_id = res; req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	int rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (!rc) { r->backing_attached = 0; d->metrics.detach_calls++; }
	return rc;
}

int uk_virtio_gpu_gl_resource_unref(struct uk_virtio_gpu_dev *d, uk_gpu_res_id res)
{
	struct ukvgpu_resource_unref req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = find_res(d, res);
	if (!r)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_UNREF, NULL, 0);
	req.resource_id = res; req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	int rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (!rc) { memset(r, 0, sizeof(*r)); d->metrics.resources_unrefed++; }
	return rc;
}

int uk_virtio_gpu_gl_caps_get(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_caps *caps)
{
	if (!d || !caps) return -EINVAL;
	*caps = (struct uk_virtio_gpu_caps){
		.num_scanouts = d->num_scanouts,
		.num_capsets = d->num_capsets,
		.device_features = d->host_features,
		.negotiated_features = d->negotiated_features,
		.has_virgl = !!(d->negotiated_features & UK_VIRTIO_GPU_F_VIRGL),
		.has_edid = !!(d->negotiated_features & UK_VIRTIO_GPU_F_EDID),
		.has_resource_uuid = !!(d->negotiated_features & UK_VIRTIO_GPU_F_RESOURCE_UUID),
		.has_resource_blob = !!(d->negotiated_features & UK_VIRTIO_GPU_F_RESOURCE_BLOB),
		.has_context_init = !!(d->negotiated_features & UK_VIRTIO_GPU_F_CONTEXT_INIT),
		.has_host_visible = !!d->host_visible.size,
		.has_blob_alignment = !!(d->negotiated_features & UK_VIRTIO_GPU_F_BLOB_ALIGNMENT),
		.blob_alignment = d->blob_alignment,
	};
	return 0;
}

int uk_virtio_gpu_dev_caps_get(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_caps *caps) { return uk_virtio_gpu_gl_caps_get(d, caps); }
int uk_virtio_gpu_caps_get(struct uk_fbdev *fbdev, struct uk_virtio_gpu_caps *caps) { return uk_virtio_gpu_dev_caps_get(uk_virtio_gpu_from_fbdev(fbdev), caps); }

int uk_virtio_gpu_gl_capset_info_get(struct uk_virtio_gpu_dev *d, uint32_t index,
		struct uk_virtio_gpu_capset_info *info)
{
	struct ukvgpu_get_capset_info req;
	struct ukvgpu_resp_capset_info resp;
	int rc;
	if (!d || !info || index >= d->num_capsets) return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_GET_CAPSET_INFO, NULL, 0);
	req.capset_index = index; req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp), UKVGPU_RESP_OK_CAPSET_INFO, NULL);
	if (rc) return rc;
	info->id = resp.capset_id; info->max_version = resp.capset_max_version; info->max_size = resp.capset_max_size;
	return 0;
}
int uk_virtio_gpu_dev_capset_info_get(struct uk_virtio_gpu_dev *d, uint32_t i, struct uk_virtio_gpu_capset_info *info) { return uk_virtio_gpu_gl_capset_info_get(d, i, info); }
int uk_virtio_gpu_capset_info_get(struct uk_fbdev *fbdev, uint32_t i, struct uk_virtio_gpu_capset_info *info) { return uk_virtio_gpu_dev_capset_info_get(uk_virtio_gpu_from_fbdev(fbdev), i, info); }

int uk_virtio_gpu_gl_capset_get(struct uk_virtio_gpu_dev *d, uint32_t capset_id, uint32_t version, void *buf, size_t len, size_t *actual_len)
{
	struct ukvgpu_get_capset req;
	struct ukvgpu_resp_capset *resp;
	int rc;
	if (!d || !buf || !len || len > UKVGPU_MAX_CAPSET_PAYLOAD) return -EINVAL;
	resp = uk_calloc(g_alloc, 1, sizeof(*resp));
	if (!resp) return -ENOMEM;
	hdr_init(d, &req.hdr, UKVGPU_CMD_GET_CAPSET, NULL, 0);
	req.capset_id = capset_id; req.capset_version = version;
	rc = cmd_submit(d, &req, sizeof(req), resp, sizeof(*resp), UKVGPU_RESP_OK_CAPSET, NULL);
	if (!rc) { memcpy(buf, resp->capset_data, len); if (actual_len) *actual_len = len; }
	uk_free(g_alloc, resp);
	return rc;
}

int uk_virtio_gpu_gl_get_edid(struct uk_virtio_gpu_dev *d, uint32_t scanout, void *buf, size_t len, size_t *actual_len)
{
	struct ukvgpu_get_edid req;
	struct ukvgpu_resp_edid resp;
	int rc;
	if (!d || !(d->negotiated_features & UK_VIRTIO_GPU_F_EDID)) return -ENOTSUP;
	if (!buf || len < sizeof(resp.edid)) return -ENOSPC;
	hdr_init(d, &req.hdr, UKVGPU_CMD_GET_EDID, NULL, 0);
	req.scanout = scanout; req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp), UKVGPU_RESP_OK_EDID, NULL);
	if (!rc) { size_t n = resp.size <= sizeof(resp.edid) ? resp.size : sizeof(resp.edid); memcpy(buf, resp.edid, n); if (actual_len) *actual_len = n; }
	return rc;
}

int uk_virtio_gpu_gl_metrics_get(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_metrics *m) { if (!d || !m) return -EINVAL; *m = d->metrics; return 0; }
int uk_virtio_gpu_gl_metrics_reset(struct uk_virtio_gpu_dev *d) { if (!d) return -EINVAL; memset(&d->metrics, 0, sizeof(d->metrics)); return 0; }

const char *uk_virtio_gpu_gl_capset_name(uint32_t id)
{
	switch (id) {
	case UK_VIRTIO_GPU_CAPSET_VIRGL: return "virgl";
	case UK_VIRTIO_GPU_CAPSET_VIRGL2: return "virgl2";
	case UK_VIRTIO_GPU_CAPSET_GFXSTREAM: return "gfxstream";
	case UK_VIRTIO_GPU_CAPSET_VENUS: return "venus";
	case UK_VIRTIO_GPU_CAPSET_CROSS_DOMAIN: return "cross-domain";
	case UK_VIRTIO_GPU_CAPSET_DRM: return "drm";
	case UK_VIRTIO_GPU_CAPSET_APIR: return "apir";
	default: return "unknown";
	}
}
const char *uk_virtio_gpu_capset_name(uint32_t id) { return uk_virtio_gpu_gl_capset_name(id); }

static struct ukvgpu_context_state *find_ctx(struct uk_virtio_gpu_dev *d,
						      uk_gpu_ctx_id id)
{
	if (!d || !id)
		return NULL;
	for (size_t i = 0; i < UKVGPU_MAX_CONTEXTS; i++)
		if (d->contexts[i].live && d->contexts[i].id == id)
			return &d->contexts[i];
	return NULL;
}

static struct ukvgpu_context_state *alloc_ctx(struct uk_virtio_gpu_dev *d)
{
	for (size_t i = 0; d && i < UKVGPU_MAX_CONTEXTS; i++) {
		if (!d->contexts[i].live) {
			memset(&d->contexts[i], 0, sizeof(d->contexts[i]));
			d->contexts[i].id = d->next_ctx++;
			d->contexts[i].live = 1;
			return &d->contexts[i];
		}
	}
	return NULL;
}

static int has_capset(struct uk_virtio_gpu_dev *d, uint32_t capset_id)
{
	struct uk_virtio_gpu_capset_info info;

	if (!d)
		return 0;
	for (uint32_t i = 0; i < d->num_capsets; i++) {
		if (!uk_virtio_gpu_gl_capset_info_get(d, i, &info) &&
		    info.id == capset_id)
			return 1;
	}
	return 0;
}

static int validate_xfer_3d(const struct ukvgpu_resource_state *r,
				 const struct uk_virtio_gpu_transfer_3d *x)
{
	uint64_t end_x, end_y, end_z, row_bytes, stride, layer_stride;
	uint64_t layer_tail, row_tail, need;

	if (!r || !x || !x->box.w || !x->box.h || !x->box.d)
		return -EINVAL;
	if (__builtin_add_overflow((uint64_t)x->box.x, (uint64_t)x->box.w, &end_x) ||
	    __builtin_add_overflow((uint64_t)x->box.y, (uint64_t)x->box.h, &end_y) ||
	    __builtin_add_overflow((uint64_t)x->box.z, (uint64_t)x->box.d, &end_z))
		return -EINVAL;
	if (end_x > r->width || end_y > r->height || end_z > r->depth)
		return -EINVAL;
	if (__builtin_mul_overflow((uint64_t)x->box.w, 4ull, &row_bytes))
		return -EINVAL;
	stride = x->stride ? x->stride : row_bytes;
	if (!stride || stride < row_bytes)
		return -EINVAL;
	if (x->layer_stride)
		layer_stride = x->layer_stride;
	else if (__builtin_mul_overflow(stride, (uint64_t)x->box.h, &layer_stride))
		return -EINVAL;
	if (!layer_stride || layer_stride < stride * (uint64_t)(x->box.h - 1ull) + row_bytes)
		return -EINVAL;
	if (__builtin_mul_overflow(layer_stride, (uint64_t)(x->box.d - 1ull), &layer_tail) ||
	    __builtin_mul_overflow(stride, (uint64_t)(x->box.h - 1ull), &row_tail) ||
	    __builtin_add_overflow(x->offset, layer_tail, &need) ||
	    __builtin_add_overflow(need, row_tail, &need) ||
	    __builtin_add_overflow(need, row_bytes, &need))
		return -EINVAL;
	return need <= r->size ? 0 : -EINVAL;
}

int uk_virtio_gpu_gl_resource_assign_uuid(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, uint8_t uuid[16])
{
	struct ukvgpu_resource_assign_uuid req;
	struct { struct ukvgpu_ctrl_hdr hdr; uint8_t uuid[16]; } resp;
	int rc;

	if (!find_res(d, res) || !uuid)
		return -EINVAL;
	if (!(d->negotiated_features & UK_VIRTIO_GPU_F_RESOURCE_UUID))
		return -ENOTSUP;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_ASSIGN_UUID, NULL, 0);
	req.resource_id = res;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_RESOURCE_UUID, NULL);
	if (!rc)
		memcpy(uuid, resp.uuid, sizeof(resp.uuid));
	return rc;
}

int uk_virtio_gpu_gl_resource_create_3d(struct uk_virtio_gpu_dev *d,
		const struct uk_virtio_gpu_resource_3d *desc, uk_gpu_res_id *out)
{
	struct ukvgpu_resource_create_3d req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r;
	int rc;

	if (!d || !desc || !out || !desc->width || !desc->height || !desc->depth)
		return -EINVAL;
	if (!(d->negotiated_features & UK_VIRTIO_GPU_F_VIRGL))
		return -ENOTSUP;
	r = alloc_res(d);
	if (!r)
		return -ENOMEM;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_CREATE_3D, NULL, 0);
	req.resource_id = r->id;
	req.target = desc->target;
	req.format = desc->format;
	req.bind = desc->bind;
	req.width = desc->width;
	req.height = desc->height;
	req.depth = desc->depth;
	req.array_size = desc->array_size;
	req.last_level = desc->last_level;
	req.nr_samples = desc->nr_samples;
	req.flags = desc->flags;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (rc) {
		memset(r, 0, sizeof(*r));
		return rc;
	}
	r->is_3d = 1;
	r->width = desc->width;
	r->height = desc->height;
	r->depth = desc->depth;
	r->format = desc->format;
	r->size = resource_size_bytes(desc->width, desc->height, desc->depth);
	*out = r->id;
	return 0;
}

static int transfer_3d(struct uk_virtio_gpu_dev *d, uk_gpu_res_id res,
		const struct uk_virtio_gpu_transfer_3d *xfer,
		uk_gpu_fence_id *fence, uint32_t cmd)
{
	struct ukvgpu_transfer_host_3d req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = find_res(d, res);
	int rc;

	rc = validate_xfer_3d(r, xfer);
	if (rc)
		return rc;
	hdr_init(d, &req.hdr, cmd, fence, 0);
	req.resource_id = res;
	req.level = xfer->level;
	req.box.x = xfer->box.x;
	req.box.y = xfer->box.y;
	req.box.z = xfer->box.z;
	req.box.w = xfer->box.w;
	req.box.h = xfer->box.h;
	req.box.d = xfer->box.d;
	req.offset = xfer->offset;
	req.stride = xfer->stride;
	req.layer_stride = xfer->layer_stride;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, fence);
	if (!rc) {
		uint64_t bytes = (uint64_t)xfer->box.w * xfer->box.h * xfer->box.d * 4ull;
		if (cmd == UKVGPU_CMD_TRANSFER_TO_HOST_3D) {
			d->metrics.transfers_to_host++;
			d->metrics.bytes_to_host += bytes;
		} else {
			d->metrics.transfers_from_host++;
			d->metrics.bytes_from_host += bytes;
		}
	}
	return rc;
}

int uk_virtio_gpu_gl_transfer_to_host_3d(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer,
		uk_gpu_fence_id *fence)
{
	return transfer_3d(d, res, xfer, fence, UKVGPU_CMD_TRANSFER_TO_HOST_3D);
}

int uk_virtio_gpu_gl_transfer_from_host_3d(struct uk_virtio_gpu_dev *d,
		uk_gpu_res_id res, const struct uk_virtio_gpu_transfer_3d *xfer,
		uk_gpu_fence_id *fence)
{
	return transfer_3d(d, res, xfer, fence, UKVGPU_CMD_TRANSFER_FROM_HOST_3D);
}

int uk_virtio_gpu_gl_context_create(struct uk_virtio_gpu_dev *d,
		uint32_t capset_id, const char *name, struct uk_virtio_gpu_context *ctx)
{
	struct ukvgpu_ctx_create req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_context_state *c;
	int rc;

	if (!d || !ctx || !capset_id)
		return -EINVAL;
	if (!(d->negotiated_features & UK_VIRTIO_GPU_F_VIRGL))
		return -ENOTSUP;
	if ((d->negotiated_features & UK_VIRTIO_GPU_F_CONTEXT_INIT) &&
	    !has_capset(d, capset_id))
		return -EINVAL;
	c = alloc_ctx(d);
	if (!c)
		return -ENOMEM;
	c->capset_id = capset_id;
	uk_gpu_fence_id fence = 0;
	hdr_init(d, &req.hdr, UKVGPU_CMD_CTX_CREATE, &fence, c->id);
	req.nlen = 0;
	req.context_init = (d->negotiated_features & UK_VIRTIO_GPU_F_CONTEXT_INIT) ?
		(capset_id & UKVGPU_CONTEXT_INIT_CAPSET_ID_MASK) : 0;
	memset(req.debug_name, 0, sizeof(req.debug_name));
	if (name) {
		size_t n = strnlen(name, sizeof(req.debug_name));
		memcpy(req.debug_name, name, n);
		req.nlen = (uint32_t)n;
	}
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (rc) {
		memset(c, 0, sizeof(*c));
		return rc;
	}
	*ctx = (struct uk_virtio_gpu_context){ .id = c->id, .capset_id = capset_id, .created = 1 };
	d->metrics.contexts_created++;
	return 0;
}

int uk_virtio_gpu_gl_context_destroy(struct uk_virtio_gpu_dev *d,
		struct uk_virtio_gpu_context *ctx)
{
	struct ukvgpu_ctrl_hdr req, resp;
	struct ukvgpu_context_state *c = ctx ? find_ctx(d, ctx->id) : NULL;
	int rc;

	if (!c)
		return -EINVAL;
	hdr_init(d, &req, UKVGPU_CMD_CTX_DESTROY, NULL, c->id);
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (!rc) {
		memset(c, 0, sizeof(*c));
		ctx->created = 0;
		d->metrics.contexts_destroyed++;
	}
	return rc;
}

static int ctx_resource(struct uk_virtio_gpu_dev *d,
		const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res,
		uint32_t cmd)
{
	struct ukvgpu_ctx_resource req;
	struct ukvgpu_ctrl_hdr resp;

	if (!find_ctx(d, ctx ? ctx->id : 0) || !find_res(d, res))
		return -EINVAL;
	hdr_init(d, &req.hdr, cmd, NULL, ctx->id);
	req.resource_id = res;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	return cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
}

int uk_virtio_gpu_gl_context_attach_resource(struct uk_virtio_gpu_dev *d,
		const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res)
{
	return ctx_resource(d, ctx, res, UKVGPU_CMD_CTX_ATTACH_RESOURCE);
}

int uk_virtio_gpu_gl_context_detach_resource(struct uk_virtio_gpu_dev *d,
		const struct uk_virtio_gpu_context *ctx, uk_gpu_res_id res)
{
	return ctx_resource(d, ctx, res, UKVGPU_CMD_CTX_DETACH_RESOURCE);
}

int uk_virtio_gpu_gl_context_submit(struct uk_virtio_gpu_dev *d,
		const struct uk_virtio_gpu_context *ctx, const void *cmd,
		size_t len, uk_gpu_fence_id *fence)
{
	struct ukvgpu_cmd_submit_3d *req;
	struct ukvgpu_ctrl_hdr resp;
	int rc;

	if (!find_ctx(d, ctx ? ctx->id : 0) || !cmd || !len || len > UINT32_MAX)
		return -EINVAL;
	req = uk_malloc(g_alloc, sizeof(*req) + len);
	if (!req)
		return -ENOMEM;
	hdr_init(d, &req->hdr, UKVGPU_CMD_SUBMIT_3D, fence, ctx->id);
	req->size = (uint32_t)len;
	req->padding = 0;
	memcpy((uint8_t *)req + sizeof(*req), cmd, len);
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, req, sizeof(*req) + len, &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, fence);
	uk_free(g_alloc, req);
	if (!rc)
		d->metrics.submits_3d++;
	return rc;
}

int uk_virtio_gpu_gl_blob_create_with_ctx(struct uk_virtio_gpu_dev *d,
		uint32_t ctx_id, uint64_t size, uint32_t mem, uint32_t flags,
		uint64_t id, struct uk_virtio_gpu_blob *blob)
{
	struct ukvgpu_resource_create_blob req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r;
	uint64_t aligned_size;
	int rc;

	if (!d || !size || !blob || blob->created)
		return -EINVAL;
	if (size > (uint64_t)SIZE_MAX)
		return -EOVERFLOW;
	if (!(d->negotiated_features & UK_VIRTIO_GPU_F_RESOURCE_BLOB))
		return -ENOTSUP;
	if (mem != UKVGPU_BLOB_MEM_GUEST && mem != UKVGPU_BLOB_MEM_HOST3D &&
	    mem != UKVGPU_BLOB_MEM_HOST3D_GUEST)
		return -EINVAL;

	/* Align blob size to blob_alignment when F_BLOB_ALIGNMENT is negotiated.
	 * Fall back to page alignment (4096) when the feature is absent. */
	{
		uint64_t align = (d->negotiated_features & UK_VIRTIO_GPU_F_BLOB_ALIGNMENT) && d->blob_alignment
			? (uint64_t)d->blob_alignment : 4096ull;
		uint64_t rounded;
		if (__builtin_add_overflow(size, align - 1ull, &rounded))
			return -EOVERFLOW;
		aligned_size = rounded & ~(align - 1ull);
		if (!aligned_size)
			aligned_size = align;
	}

	r = alloc_res(d);
	if (!r)
		return -ENOMEM;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_CREATE_BLOB, NULL, ctx_id);
	req.resource_id = r->id;
	req.blob_mem = mem;
	req.blob_flags = flags;
	req.nr_entries = 0;
	req.blob_id = id;
	req.size = aligned_size;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (rc) {
		memset(r, 0, sizeof(*r));
		return rc;
	}
	r->is_3d = 1;
	r->is_blob = 1;
	r->size = aligned_size;
	r->blob_mem = mem;
	r->blob_flags = flags;
	r->blob_id = id;
	r->host_visible_offset = d->next_blob_map_offset;
	{
		uint64_t next;
		if (__builtin_add_overflow(d->next_blob_map_offset, aligned_size, &next)) {
			memset(r, 0, sizeof(*r));
			return -EOVERFLOW;
		}
		d->next_blob_map_offset = next;
	}
	*blob = (struct uk_virtio_gpu_blob){
		.resource_id = r->id, .blob_mem = mem, .blob_flags = flags,
		.blob_id = id, .size = aligned_size, .host_visible_offset = r->host_visible_offset,
		.created = 1,
	};
	d->metrics.blobs_created++;
	return 0;
}

int uk_virtio_gpu_gl_blob_create(struct uk_virtio_gpu_dev *d, uint64_t size,
		uint32_t mem, uint32_t flags, uint64_t id,
		struct uk_virtio_gpu_blob *blob)
{
	/* cmd_submit is performed by uk_virtio_gpu_gl_blob_create_with_ctx(). */
	return uk_virtio_gpu_gl_blob_create_with_ctx(d, 0, size, mem, flags,
						    id, blob);
}

int uk_virtio_gpu_gl_blob_map(struct uk_virtio_gpu_dev *d,
		struct uk_virtio_gpu_blob *blob)
{
	struct ukvgpu_resource_map_blob req;
	struct ukvgpu_resp_map_info resp;
	struct ukvgpu_resource_state *r = blob ? find_res(d, blob->resource_id) : NULL;
	int rc;

	if (!r || !r->is_blob || r->mapped || !blob->created)
		return -EINVAL;
	if (!(blob->blob_flags & UKVGPU_BLOB_FLAG_USE_MAPPABLE))
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_MAP_BLOB, NULL, 0);
	req.resource_id = blob->resource_id;
	req.padding = 0;
	req.offset = r->host_visible_offset;
	printf("VOGUE-DBG map_blob res=%u off=0x%llx size=0x%llx hv_addr=%p hv_size=0x%llx\n",
	       blob->resource_id, (unsigned long long)r->host_visible_offset,
	       (unsigned long long)blob->size, d->host_visible.addr,
	       (unsigned long long)d->host_visible.size);
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_MAP_INFO, NULL);
	if (rc)
		return rc;
	r->map_info = resp.map_info;
	r->mapped = 1;
	blob->map_info = resp.map_info;
	blob->host_visible_offset = r->host_visible_offset;
	blob->mapped_size = blob->size;
	if (d->host_visible.addr &&
	    r->host_visible_offset + blob->size <= d->host_visible.size) {
		blob->mapped_addr = (uint8_t *)d->host_visible.addr + r->host_visible_offset;
		blob->reply_notif = (volatile uint32_t *)blob->mapped_addr;
		blob->mapped = 1;
		d->metrics.blobs_mapped++;
		return 0;
	}
	/* Current local Unikraft virtio-pci exposes no shared-memory BAR helper.
	 * Undo the host map before returning a truthful fail-closed status so the
	 * resource remains destroyable and callers do not observe a half-mapped BO.
	 */
	blob->mapped = 1;
	(void)uk_virtio_gpu_gl_blob_unmap(d, blob);
	return -ENOTSUP;
}

int uk_virtio_gpu_gl_blob_unmap(struct uk_virtio_gpu_dev *d,
		struct uk_virtio_gpu_blob *blob)
{
	struct ukvgpu_resource_unmap_blob req;
	struct ukvgpu_ctrl_hdr resp;
	struct ukvgpu_resource_state *r = blob ? find_res(d, blob->resource_id) : NULL;
	int rc;

	if (!r || !r->is_blob || !r->mapped)
		return -EINVAL;
	hdr_init(d, &req.hdr, UKVGPU_CMD_RESOURCE_UNMAP_BLOB, NULL, 0);
	req.resource_id = blob->resource_id;
	req.padding = 0;
	memset(&resp, 0, sizeof(resp));
	rc = cmd_submit(d, &req, sizeof(req), &resp, sizeof(resp),
			UKVGPU_RESP_OK_NODATA, NULL);
	if (!rc) {
		r->mapped = 0;
		blob->mapped = 0;
		blob->mapped_addr = NULL;
		blob->mapped_size = 0;
		blob->reply_notif = NULL;
		d->metrics.blobs_unmapped++;
	}
	return rc;
}

int uk_virtio_gpu_gl_blob_destroy(struct uk_virtio_gpu_dev *d,
		struct uk_virtio_gpu_blob *blob)
{
	if (!blob || !blob->created || blob->mapped)
		return -EINVAL;
	if (uk_virtio_gpu_gl_resource_unref(d, blob->resource_id))
		return -EINVAL;
	memset(blob, 0, sizeof(*blob));
	return 0;
}

int uk_virtio_gpu_dev_shm_region_get(struct uk_virtio_gpu_dev *d, uint32_t id,
		struct uk_virtio_gpu_shm_region *r)
{
	if (!d || !r)
		return -EINVAL;
	if (id != UKVGPU_SHM_ID_HOST_VISIBLE || !d->host_visible.size)
		return -ENOTSUP;
	*r = d->host_visible;
	return 0;
}

int uk_virtio_gpu_context_create(struct uk_virtio_gpu_dev *d, uint32_t c, const char *n, struct uk_virtio_gpu_context *ctx) { return uk_virtio_gpu_gl_context_create(d, c, n, ctx); }
int uk_virtio_gpu_context_destroy(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_context *ctx) { return uk_virtio_gpu_gl_context_destroy(d, ctx); }
int uk_virtio_gpu_context_attach_blob(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *b) { return b ? uk_virtio_gpu_gl_context_attach_resource(d, ctx, b->resource_id) : -EINVAL; }
int uk_virtio_gpu_context_detach_blob(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *b) { return b ? uk_virtio_gpu_gl_context_detach_resource(d, ctx, b->resource_id) : -EINVAL; }
int uk_virtio_gpu_context_submit(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const void *cmd, size_t len) { uk_gpu_fence_id f; return uk_virtio_gpu_gl_context_submit(d, ctx, cmd, len, &f); }
int uk_virtio_gpu_blob_create(struct uk_virtio_gpu_dev *d, uint64_t s, uint32_t m, uint32_t f, uint64_t id, struct uk_virtio_gpu_blob *b) { return uk_virtio_gpu_gl_blob_create(d, s, m, f, id, b); }
int uk_virtio_gpu_blob_map(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_blob *b) { return uk_virtio_gpu_gl_blob_map(d, b); }
int uk_virtio_gpu_blob_unmap(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_blob *b) { return uk_virtio_gpu_gl_blob_unmap(d, b); }
int uk_virtio_gpu_blob_destroy(struct uk_virtio_gpu_dev *d, struct uk_virtio_gpu_blob *b) { return uk_virtio_gpu_gl_blob_destroy(d, b); }
struct uk_virtio_gpu_dev *uk_virtio_gpu_from_fbdev(struct uk_fbdev *fbdev) { (void)fbdev; return NULL; }

struct uk_virtio_gpu_dev *uk_virtio_gpu_default_dev(void) { return g_default_dev; }
int uk_virtio_gpu_probe(struct uk_virtio_gpu_dev **out)
{
	if (!out) return -EINVAL;
	*out = g_default_dev;
	return g_default_dev ? 0 : -ENODEV;
}

static int ctrlq_cb(struct virtqueue *vq, void *priv) { (void)vq; (void)priv; return 1; }

static void fail_dev(struct virtio_dev *vdev)
{
	virtio_dev_status_update(vdev, VIRTIO_CONFIG_STATUS_FAIL);
}

static int vgpu_add_dev(struct virtio_dev *vdev)
{
	struct uk_virtio_gpu_dev *d;
	uint16_t qsz[UKVGPU_NR_QUEUES] = {0};
	uint64_t host, features = 0;
	int rc;

	d = uk_calloc(g_alloc, 1, sizeof(*d));
	if (!d) return -ENOMEM;
	d->vdev = vdev; d->alloc = g_alloc; d->next_res = 1; d->next_ctx = 1; d->next_fence = 1; d->next_blob_map_offset = 0; d->metrics.probes = 1;
	vdev->priv = d;

	host = virtio_feature_get(vdev);
	d->host_features = host;
	if (VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_VIRGL)) features |= UK_VIRTIO_GPU_F_VIRGL;
	if (VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_EDID)) features |= UK_VIRTIO_GPU_F_EDID;
	if (VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_RESOURCE_UUID)) features |= UK_VIRTIO_GPU_F_RESOURCE_UUID;
	if (VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_RESOURCE_BLOB)) features |= UK_VIRTIO_GPU_F_RESOURCE_BLOB;
	if ((features & UK_VIRTIO_GPU_F_VIRGL) && VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_CONTEXT_INIT)) features |= UK_VIRTIO_GPU_F_CONTEXT_INIT;
	if ((features & UK_VIRTIO_GPU_F_RESOURCE_BLOB) && VIRTIO_FEATURE_HAS(host, VIRTIO_GPU_F_BLOB_ALIGNMENT)) features |= UK_VIRTIO_GPU_F_BLOB_ALIGNMENT;
	vdev->features = features;
	virtio_feature_set(vdev);
	virtio_dev_status_update(vdev, VIRTIO_CONFIG_STATUS_FEATURES_OK);
	if (!(virtio_dev_status_get(vdev) & VIRTIO_CONFIG_STATUS_FEATURES_OK)) { rc = -EINVAL; goto fail; }
	d->negotiated_features = features;

	rc = virtio_config_get(vdev, offsetof(struct ukvgpu_config, events_read), &d->events_read, sizeof(d->events_read), 4); if (rc < 0) goto fail;
	rc = virtio_config_get(vdev, offsetof(struct ukvgpu_config, num_scanouts), &d->num_scanouts, sizeof(d->num_scanouts), 4); if (rc < 0) goto fail;
	rc = virtio_config_get(vdev, offsetof(struct ukvgpu_config, num_capsets), &d->num_capsets, sizeof(d->num_capsets), 4); if (rc < 0) goto fail;
	if (!d->num_scanouts || d->num_scanouts > UKVGPU_MAX_SCANOUTS) d->num_scanouts = 1;
	if (features & UK_VIRTIO_GPU_F_BLOB_ALIGNMENT) {
		rc = virtio_config_get(vdev, offsetof(struct ukvgpu_config, blob_alignment), &d->blob_alignment, sizeof(d->blob_alignment), 4);
		if (rc < 0 || !d->blob_alignment || (d->blob_alignment & (d->blob_alignment - 1))) {
			/* Invalid or unreadable blob_alignment — fall back to page alignment. */
			d->blob_alignment = 4096u;
			uk_pr_warn(DRIVER_NAME": blob_alignment invalid (rc=%d val=%u), using 4096\n", rc, d->blob_alignment);
		} else {
			uk_pr_info(DRIVER_NAME": blob_alignment=%u\n", d->blob_alignment);
		}
	}
	if (features & UK_VIRTIO_GPU_F_RESOURCE_BLOB) {
		void *shm_base = NULL;
		uint64_t shm_len = 0;

		rc = virtio_pci_shm_region_get(vdev, UKVGPU_SHM_ID_HOST_VISIBLE,
					       &shm_base, &shm_len);
		if (!rc && shm_base && shm_len) {
			d->host_visible = (struct uk_virtio_gpu_shm_region){
				.id = UKVGPU_SHM_ID_HOST_VISIBLE,
				.size = shm_len,
				.addr = shm_base,
			};
			uk_pr_info(DRIVER_NAME": host-visible shm id=%u addr=%p size=0x%llx\n",
				   UKVGPU_SHM_ID_HOST_VISIBLE, shm_base,
				   (unsigned long long)shm_len);
		} else {
			uk_pr_warn(DRIVER_NAME": host-visible shm unavailable rc=%d\n",
				   rc);
		}
	}

	rc = virtio_find_vqs(vdev, UKVGPU_NR_QUEUES, qsz);
	if (rc < 1) goto fail;
	d->ctrlq = virtio_vqueue_setup(vdev, UKVGPU_CTRLQ, qsz[0] ? qsz[0] : 16, ctrlq_cb, g_alloc);
	if (PTRISERR(d->ctrlq)) { rc = PTR2ERR(d->ctrlq); d->ctrlq = NULL; goto fail; }
	d->ctrlq->priv = d;
	if (rc >= 2) {
		d->cursorq = virtio_vqueue_setup(vdev, UKVGPU_CURSORQ, qsz[1] ? qsz[1] : 16, ctrlq_cb, g_alloc);
		if (!PTRISERR(d->cursorq)) d->cursorq->priv = d; else d->cursorq = NULL;
	}
	virtqueue_intr_enable(d->ctrlq);
	if (d->cursorq) virtqueue_intr_enable(d->cursorq);
	virtio_dev_drv_up(vdev);

	rc = uk_virtio_gpu_get_display_info(d);
	if (rc) goto fail;
	g_default_dev = d;
	uk_pr_info(DRIVER_NAME": real virtio-gpu device id=16 scanouts=%u capsets=%u features=0x%lx\n", d->num_scanouts, d->num_capsets, d->negotiated_features);
	return 0;
fail:
	fail_dev(vdev);
	if (d->ctrlq) virtio_vqueue_release(vdev, d->ctrlq, g_alloc);
	if (d->cursorq) virtio_vqueue_release(vdev, d->cursorq, g_alloc);
	uk_free(g_alloc, d);
	return rc;
}

static int vgpu_drv_init(struct uk_alloc *a)
{
	if (!a) return -EINVAL;
	g_alloc = a;
	return 0;
}

static const struct virtio_dev_id vgpu_ids[] = {
	{ VIRTIO_ID_GPU },
	{ VIRTIO_ID_INVALID },
};

static struct virtio_driver vgpu_drv = {
	.dev_ids = vgpu_ids,
	.init = vgpu_drv_init,
	.add_dev = vgpu_add_dev,
};

VIRTIO_BUS_REGISTER_DRIVER(&vgpu_drv);

static int apir_put_u32(uint8_t *buf, size_t len, size_t *off, uint32_t v)
{
	if (!buf || !off || *off > len || len - *off < sizeof(v)) return -ENOSPC;
	memcpy(buf + *off, &v, sizeof(v)); *off += sizeof(v); return 0;
}
static int apir_get_u32(const uint8_t *buf, size_t len, size_t *off, uint32_t *v)
{
	if (!buf || !off || !v || *off > len || len - *off < sizeof(*v)) return -EINVAL;
	memcpy(v, buf + *off, sizeof(*v)); *off += sizeof(*v); return 0;
}
int uk_virtio_gpu_apir_encode_header(void *buf, size_t len, const struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0; int rc;
	if (!buf || !msg || msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH) return -EINVAL;
	rc = apir_put_u32(buf, len, &off, msg->command_type); if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->flags); if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, msg->reply_resource_id); if (rc) return rc;
	if (used) *used = off; return 0;
}
int uk_virtio_gpu_apir_decode_header(const void *buf, size_t len, struct uk_virtio_gpu_apir_msg *msg, size_t *used)
{
	size_t off = 0; int rc;
	if (!buf || !msg) return -EINVAL;
	rc = apir_get_u32(buf, len, &off, &msg->command_type); if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->flags); if (rc) return rc;
	rc = apir_get_u32(buf, len, &off, &msg->reply_resource_id); if (rc) return rc;
	if (msg->command_type >= UK_VIRTIO_GPU_APIR_COMMAND_LENGTH) return -EINVAL;
	if (used) *used = off; return 0;
}
int uk_virtio_gpu_apir_encode_handshake(void *buf, size_t len, uint32_t reply_resource_id, size_t *used)
{
	struct uk_virtio_gpu_apir_msg msg = { UK_VIRTIO_GPU_APIR_COMMAND_HANDSHAKE, 0, reply_resource_id };
	size_t off; int rc = uk_virtio_gpu_apir_encode_header(buf, len, &msg, &off); if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR); if (rc) return rc;
	rc = apir_put_u32(buf, len, &off, UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR); if (rc) return rc;
	if (used) *used = off; return 0;
}
int uk_virtio_gpu_apir_decode_handshake_reply(const void *buf, size_t len, uint32_t host_rc, struct uk_virtio_gpu_apir_handshake *reply)
{
	(void)buf; (void)len;
	if (!reply || host_rc != UK_VIRTIO_GPU_APIR_HANDSHAKE_MAGIC) return -EINVAL;
	reply->guest_major = UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR; reply->guest_minor = UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR;
	reply->host_major = UK_VIRTIO_GPU_APIR_PROTOCOL_MAJOR; reply->host_minor = UK_VIRTIO_GPU_APIR_PROTOCOL_MINOR;
	return 0;
}
int uk_virtio_gpu_apir_handshake(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob, struct uk_virtio_gpu_apir_handshake *reply, struct uk_virtio_gpu_apir_status *status)
{
	(void)d; (void)ctx; (void)blob; (void)reply; if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP }; return -ENOTSUP;
}
int uk_virtio_gpu_apir_load_library(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob, struct uk_virtio_gpu_apir_status *status)
{
	(void)d; (void)ctx; (void)blob; if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP }; return -ENOTSUP;
}
int uk_virtio_gpu_apir_forward(struct uk_virtio_gpu_dev *d, const struct uk_virtio_gpu_context *ctx, const struct uk_virtio_gpu_blob *blob, uint32_t flags, const void *payload, size_t payload_len, struct uk_virtio_gpu_apir_status *status)
{
	(void)d; (void)ctx; (void)blob; (void)flags; (void)payload; (void)payload_len; if (status) *status = (struct uk_virtio_gpu_apir_status){ .transport_rc = (uint32_t)-ENOTSUP }; return -ENOTSUP;
}
