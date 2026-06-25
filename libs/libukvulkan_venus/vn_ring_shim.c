/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Mesa alignment:
 *   Analogue: src/virtio/vulkan/vn_ring.h submit/reply surface.
 *   Same: provide the vn_ring_* callbacks required by generated wrappers.
 *   VOGUE adaptation: bind callbacks to the process-current Unikraft Venus context.
 */
/*
 * vn_ring_shim.c
 *
 * Implements the four-function vn_ring surface the generated vn_submit_* /
 * vn_call_* wrappers expect, bridging onto the existing uk_venus_submit
 * transport. A process-wide "current" device+context is bound by
 * uk_venus_ring_bind_current() before any wrapper runs.
 *
 * The encode-only cutover (M5/M6) invokes the generated vn_encode_* directly,
 * so these wrappers are normally dead-code-eliminated. They are implemented
 * (not stubbed) so the optional vn_call_* round-trip path links and works for
 * non-reply commands.
 */
#include <uk/vn_ring.h>
#include <uk/venus.h>
#include <string.h>

static struct uk_virtio_gpu_dev *g_dev;
static struct uk_virtio_gpu_context *g_ctx;

void uk_venus_ring_bind_current(struct uk_virtio_gpu_dev *dev,
				struct uk_virtio_gpu_context *ctx)
{
	g_dev = dev;
	g_ctx = ctx;
}

struct vn_cs_encoder *
vn_ring_submit_command_init(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit,
			    void *cmd_data, size_t cmd_size, size_t reply_size)
{
	(void)ring;
	if (!cmd_size)
		return NULL;
	uk_venus_encoder_init(&submit->backing, cmd_data, cmd_size);
	submit->enc.e      = &submit->backing;
	submit->cmd_data   = cmd_data;
	submit->cmd_size   = cmd_size;
	submit->reply_size = reply_size;
	submit->reply_len  = 0;
	submit->has_reply  = 0;
	return &submit->enc;
}

void vn_ring_submit_command(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit)
{
	(void)ring;
	if (!g_dev || !g_ctx)
		return;
	if (submit->reply_size) {
		uint32_t cmd_type = 0;
		uint32_t cap = submit->reply_size < sizeof(submit->reply_data) ?
			(uint32_t)submit->reply_size :
			(uint32_t)sizeof(submit->reply_data);
		int n = uk_venus_query_reply(g_dev, g_ctx, submit->backing.buf,
					     (uint32_t)submit->backing.pos,
					     submit->reply_data, cap);
		if (n >= (int)sizeof(cmd_type)) {
			memcpy(&cmd_type, submit->reply_data, sizeof(cmd_type));
			if (cmd_type != 0) {
				submit->reply.cur = submit->reply_data;
				submit->reply.end = submit->reply_data + (size_t)n;
				submit->reply.fatal = 0;
				submit->reply_len = (size_t)n;
				submit->has_reply = 1;
			}
		}
		return;
	}
	(void)uk_venus_submit(g_dev, g_ctx, &submit->backing);
}

struct vn_cs_decoder *
vn_ring_get_command_reply(struct vn_ring *ring,
			  struct vn_ring_submit_command *submit)
{
	(void)ring;
	return submit->has_reply ? &submit->reply : NULL;
}

void vn_ring_free_command_reply(struct vn_ring *ring,
				struct vn_ring_submit_command *submit)
{
	(void)ring;
	(void)submit;
}
