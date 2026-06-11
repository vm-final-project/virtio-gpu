/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uk/venus.h>

#include "test_utils.h"

static uint32_t read_u32(const uint8_t *p, size_t off)
{
	uint32_t v;

	memcpy(&v, p + off, sizeof(v));
	return v;
}

static uint64_t read_u64(const uint8_t *p, size_t off)
{
	uint64_t v;

	memcpy(&v, p + off, sizeof(v));
	return v;
}

int main(void)
{
	struct test_state t = { .suite = "venus_encoder_test" };
	struct uk_venus_encoder enc;
	uint8_t buf[2048];
	uint64_t handles[2] = { 0x100000001ull, 0x100000002ull };
	uint64_t fence = 0xB0000001ull;

	TEST_CHECK(&t, "init rejects null encoder", uk_venus_encoder_init(NULL, buf, sizeof(buf)) < 0);
	TEST_CHECK(&t, "init rejects null buffer", uk_venus_encoder_init(&enc, NULL, sizeof(buf)) < 0);
	TEST_CHECK(&t, "init rejects zero capacity", uk_venus_encoder_init(&enc, buf, 0) < 0);
	TEST_CHECK(&t, "init works", uk_venus_encoder_init(&enc, buf, sizeof(buf)) == 0);
	TEST_CHECK(&t, "size starts at zero", uk_venus_encoder_size(&enc) == 0);

	uk_venus_encode_uint32(&enc, 0x12345678u);
	uk_venus_encode_uint64(&enc, 0xCAFEBABEDEADBEEFull);
	TEST_CHECK(&t, "uint32 encoded", read_u32(buf, 0) == 0x12345678u);
	TEST_CHECK(&t, "uint64 packed", read_u64(buf, 4) == 0xCAFEBABEDEADBEEFull);
	TEST_CHECK(&t, "packed size", uk_venus_encoder_size(&enc) == 12);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_cstring(&enc, "ab");
	TEST_CHECK(&t, "cstring size", uk_venus_encoder_size(&enc) == 12);
	TEST_CHECK(&t, "cstring count", read_u64(buf, 0) == 3u);
	TEST_CHECK(&t, "cstring bytes", buf[8] == 'a' && buf[9] == 'b' && buf[10] == '\0');

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkCreateInstance(&enc, 0xABCD0001ull, "unikraft-test", 0x00401000u, 0, NULL);
	TEST_CHECK(&t, "create instance cmd", read_u32(buf, 0) == VN_CMD_vkCreateInstance);
	TEST_CHECK(&t, "create instance flags", read_u32(buf, 4) == VN_COMMAND_FLAGS_NONE);
	TEST_CHECK(&t, "create instance pointer flag", read_u64(buf, 8) == VN_PTR_PRESENT);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkEnumeratePhysicalDevices(&enc, 0xABCD0001ull, 2u, handles);
	TEST_CHECK(&t, "enumerate cmd", read_u32(buf, 0) == VN_CMD_vkEnumeratePhysicalDevices);
	TEST_CHECK(&t, "enumerate non-empty", uk_venus_encoder_size(&enc) > 0);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkCreateBuffer(&enc, 0xDEAD0001ull, 0xBEEF0001ull, 65536u, 0x80u);
	TEST_CHECK(&t, "create buffer cmd", read_u32(buf, 0) == VN_CMD_vkCreateBuffer);
	TEST_CHECK(&t, "create buffer device", read_u64(buf, 8) == 0xDEAD0001ull);
	TEST_CHECK(&t, "create buffer usage", read_u32(buf, 48) == 0x80u);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkAllocateMemory(&enc, 0xDEAD0001ull, 0xAA000001ull, 1024u * 1024u, 2u);
	TEST_CHECK(&t, "allocate memory cmd", read_u32(buf, 0) == VN_CMD_vkAllocateMemory);
	TEST_CHECK(&t, "allocate memory size", read_u64(buf, 36) == 1024u * 1024u);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkCreateFence(&enc, 0xDEAD0001ull, fence, 0);
	uk_venus_encode_vkQueueSubmit(&enc, 0xB1000001ull, 1u, handles, fence);
	uk_venus_encode_vkWaitForFences(&enc, 0xDEAD0001ull, 1u, &fence, 5000000000ull);
	TEST_CHECK(&t, "composite encode has bytes", uk_venus_encoder_size(&enc) > 0);
	TEST_CHECK(&t, "composite encode no overflow", !uk_venus_encoder_overflow(&enc));

	{
		uint8_t tiny[4];
		TEST_CHECK(&t, "tiny init", uk_venus_encoder_init(&enc, tiny, sizeof(tiny)) == 0);
		uk_venus_encode_uint32(&enc, 0xDEADBEEFu);
		uk_venus_encode_uint32(&enc, 1u);
		TEST_CHECK(&t, "overflow flagged", uk_venus_encoder_overflow(&enc));
	}

	return test_finish(&t);
}
