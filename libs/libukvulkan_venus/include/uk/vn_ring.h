/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libs/libukvulkan_venus/include/uk/vn_ring.h
 *
 * Satisfies the vn_submit_* / vn_call_* wrappers in the generated headers.
 * The surface (verified against the generated tree) is exactly four functions
 * plus one struct that the generated code only ever passes by address:
 *
 *     vn_ring_submit_command_init / vn_ring_submit_command
 *     vn_ring_get_command_reply   / vn_ring_free_command_reply
 *
 * These map onto the existing struct uk_venus_ring + uk_venus_submit transport
 * via a thunk bound by uk_venus_ring_bind_current() (see ring/venus_ring.c). The
 * encode-only cutover (M5/M6) calls the generated vn_encode_* directly, so the
 * submit/call wrappers are dead-code-eliminated; this header exists so the
 * generated tree compiles and so the optional vn_call_* path stays available.
 */
#ifndef VN_RING_H
#define VN_RING_H

#include <stddef.h>
#include <uk/venus.h>     /* struct uk_venus_ring, uk_venus_* */
#include <uk/vn_cs.h>

/* Mesa tracing hook used by the generated vn_call_* wrappers; no-op in VOGUE. */
#ifndef VN_TRACE_FUNC
#define VN_TRACE_FUNC() do { } while (0)
#endif

struct vn_ring;           /* opaque; the thunk binds a uk_venus_ring + dev/ctx */

struct vn_ring_submit_command {
	struct vn_cs_encoder enc;        /* encode target for this command */
	struct uk_venus_encoder backing; /* owns the byte buffer */
	void *cmd_data;
	size_t cmd_size;
	size_t reply_size;
	uint8_t reply_data[4096];
	size_t reply_len;
	struct vn_cs_decoder reply;      /* valid after submit when reply_size>0 */
	int has_reply;
};

struct vn_cs_encoder *
vn_ring_submit_command_init(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit,
			    void *cmd_data, size_t cmd_size, size_t reply_size);

void vn_ring_submit_command(struct vn_ring *ring,
			    struct vn_ring_submit_command *submit);

struct vn_cs_decoder *
vn_ring_get_command_reply(struct vn_ring *ring,
			  struct vn_ring_submit_command *submit);

void vn_ring_free_command_reply(struct vn_ring *ring,
				struct vn_ring_submit_command *submit);

#endif /* VN_RING_H */
