/* SPDX-License-Identifier: BSD-3-Clause */
#include <string.h>

#include <uk/test.h>
#include <uk/venus.h>

static uint32_t test_read_u32(const uint8_t *buf, size_t off)
{
	uint32_t v = 0;

	memcpy(&v, buf + off, sizeof(v));
	return v;
}

static uint64_t test_read_u64(const uint8_t *buf, size_t off)
{
	uint64_t v = 0;

	memcpy(&v, buf + off, sizeof(v));
	return v;
}

UK_TESTCASE(libukvulkan_venus, encoder_basics)
{
	struct uk_venus_encoder enc;
	uint8_t buf[256];
	uint64_t packed = 0xCAFEBABEDEADBEEFULL;

	UK_TEST_EXPECT_SNUM_LT(uk_venus_encoder_init(NULL, buf, sizeof(buf)), 0);
	UK_TEST_EXPECT_SNUM_LT(uk_venus_encoder_init(&enc, NULL, sizeof(buf)), 0);
	UK_TEST_EXPECT_SNUM_LT(uk_venus_encoder_init(&enc, buf, 0), 0);
	UK_TEST_EXPECT_ZERO(uk_venus_encoder_init(&enc, buf, sizeof(buf)));
	UK_TEST_EXPECT_ZERO(uk_venus_encoder_size(&enc));

	uk_venus_encode_uint32(&enc, 0x12345678u);
	uk_venus_encode_uint64(&enc, packed);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u32(buf, 0), 0x12345678L);
	UK_TEST_EXPECT_BYTES_EQ(&packed, buf + 4, sizeof(packed));
	UK_TEST_EXPECT_SNUM_EQ((long)uk_venus_encoder_size(&enc), 12L);
}

UK_TESTCASE(libukvulkan_venus, encode_bridge_commands)
{
	struct uk_venus_encoder enc;
	uint8_t buf[512];
	uint64_t handles[2] = { 0x100000001ull, 0x100000002ull };

	UK_TEST_EXPECT_ZERO(uk_venus_encoder_init(&enc, buf, sizeof(buf)));

	uk_venus_encode_vkCreateInstance(&enc, 0xABCD0001ull, "unikraft-test",
					 0x00401000u, 0, NULL);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u32(buf, 0), VN_CMD_vkCreateInstance);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u32(buf, 4), VN_COMMAND_FLAGS_NONE);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u64(buf, 8), VN_PTR_PRESENT);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkEnumeratePhysicalDevices(&enc, 0xABCD0001ull, 2u, handles);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u32(buf, 0),
			       VN_CMD_vkEnumeratePhysicalDevices);
	UK_TEST_EXPECT_SNUM_GT((long)uk_venus_encoder_size(&enc), 0L);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkCreateBuffer(&enc, 0xDEAD0001ull, 0xBEEF0001ull, 65536u,
				       0x80u);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u32(buf, 0), VN_CMD_vkCreateBuffer);
	UK_TEST_EXPECT_SNUM_EQ((long)test_read_u64(buf, 8), 0xDEAD0001LL);
}

UK_TESTCASE(libukvulkan_venus, overflow_flag)
{
	struct uk_venus_encoder enc;
	uint8_t tiny[4];

	UK_TEST_EXPECT_ZERO(uk_venus_encoder_init(&enc, tiny, sizeof(tiny)));
	uk_venus_encode_uint32(&enc, 0xDEADBEEFu);
	uk_venus_encode_uint32(&enc, 1u);
	UK_TEST_EXPECT_NOT_ZERO(uk_venus_encoder_overflow(&enc));
}

uk_testsuite_register(libukvulkan_venus, NULL);
