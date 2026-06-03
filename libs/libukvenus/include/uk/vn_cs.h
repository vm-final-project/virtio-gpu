/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * libs/libukvenus/include/uk/vn_cs.h
 *
 * Unikraft adapter satisfying the interface documented in the upstream
 * venus-protocol driver_cs.h template:
 *
 *     struct vn_cs_encoder, vn_cs_encoder_{get_len,reserve,write}
 *     struct vn_cs_decoder, vn_cs_decoder_{set_fatal,read,peek}
 *     vn_cs_handle_{load,store}_id
 *
 * The generated vn_encode_* / vn_decode_* inline functions are layered on top
 * of these. Encoding maps onto struct uk_venus_encoder; Vulkan handles are
 * bare uint64 guest ids in VOGUE, so id == (uintptr_t)handle.
 *
 * Note: VN_SUBMIT_LOCAL_CMD_SIZE is defined by the generated
 * vn_protocol_driver_defines.h, not here, to avoid a redefinition conflict.
 */
#ifndef VN_CS_H
#define VN_CS_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <uk/venus.h>   /* struct uk_venus_encoder + uk_venus_encode_bytes */

struct vn_cs_encoder {
	struct uk_venus_encoder *e;
};

struct vn_cs_decoder {
	const uint8_t *cur;
	const uint8_t *end;
	int fatal;
};

/* --- encoder --- */
static inline size_t vn_cs_encoder_get_len(const struct vn_cs_encoder *enc)
{
	return uk_venus_encoder_size(enc->e);
}

/* Fixed-capacity backing buffer: reserve is advisory. uk_venus_encoder tracks
 * overflow internally, so a non-NULL return just means "proceed". */
static inline void *vn_cs_encoder_reserve(struct vn_cs_encoder *enc, size_t size)
{
	(void)size;
	return enc;
}

static inline void vn_cs_encoder_write(struct vn_cs_encoder *enc, size_t size,
				       const void *data, size_t data_size)
{
	assert(size % 4 == 0);
	uk_venus_encode_bytes(enc->e, data, data_size);
	if (size > data_size)             /* zero-pad to the 4-byte wire stride */
		uk_venus_encode_bytes(enc->e, NULL, size - data_size);
}

/* --- decoder (reply parsing) --- */
static inline void vn_cs_decoder_set_fatal(struct vn_cs_decoder *dec)
{
	dec->fatal = 1;
}

static inline void vn_cs_decoder_read(struct vn_cs_decoder *dec, size_t size,
				      void *data, size_t data_size)
{
	assert(size % 4 == 0);
	if (dec->cur + size > dec->end) {
		dec->fatal = 1;
		return;
	}
	memcpy(data, dec->cur, data_size);
	dec->cur += size;
}

static inline void *vn_cs_decoder_peek(struct vn_cs_decoder *dec, size_t size,
				       void *data, size_t data_size)
{
	(void)size;
	if (dec->cur + data_size > dec->end) {
		dec->fatal = 1;
		return NULL;
	}
	memcpy(data, dec->cur, data_size);
	return (void *)dec->cur;
}

/* --- renderer protocol capability gates ---
 * The generated pNext walkers gate optional extension structs on these. VOGUE
 * only ever builds minimal structs with pNext == NULL, so these branches are
 * unreachable at runtime; they must still link. Report a fully-capable host. */
static inline bool vn_cs_renderer_protocol_has_extension(uint32_t ext_number)
{
	(void)ext_number;
	return true;
}

static inline bool vn_cs_renderer_protocol_has_api_version(uint32_t api_version)
{
	(void)api_version;
	return true;
}

/* --- handle <-> guest id --- */
static inline uint64_t vn_cs_handle_load_id(const void **handle, VkObjectType t)
{
	(void)t;
	return (uint64_t)(uintptr_t)*handle;
}

static inline void vn_cs_handle_store_id(void **handle, uint64_t id,
					 VkObjectType t)
{
	(void)t;
	*handle = (void *)(uintptr_t)id;
}

#endif /* VN_CS_H */
