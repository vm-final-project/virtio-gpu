/*
 * venus_cs_test.c — Unit tests for libukvenus command-stream encoder.
 *
 * Tests the Venus wire-format encoder without requiring a real VirtIO-GPU
 * device.  Validates:
 *   - uk_venus_encoder_init / reset / size / overflow
 *   - uk_venus_encode_uint32 / uint64 / cstring / pointer_flag
 *   - uk_venus_encode_vkCreateInstance (structure/field layout)
 *   - uk_venus_encode_vkEnumeratePhysicalDevices
 *   - uk_venus_probe / capset_get (fake device stub path)
 *
 * Each check is printed as:  PASS  <name>  or  FAIL  <name>: <reason>
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <uk/venus.h>

#ifndef VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA
#define VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA 1000384000u
#endif

/* ------------------------------------------------------------------ helpers */

static int g_fail_count;

#define CHECK(name, cond) do { \
	if (cond) { \
		printf("  PASS  " name "\n"); \
	} else { \
		printf("  FAIL  " name "\n"); \
		g_fail_count++; \
	} \
} while (0)

static uint32_t read_u32(const uint8_t *p) {
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static uint64_t read_u64(const uint8_t *p) {
	uint64_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

/* --------------------------------------------------------- encoder lifecycle */

static void test_encoder_init(void)
{
	uint8_t buf[64];
	struct uk_venus_encoder enc;

	CHECK("init:null_enc",  uk_venus_encoder_init(NULL, buf, 64) < 0);
	CHECK("init:null_buf",  uk_venus_encoder_init(&enc, NULL, 64) < 0);
	CHECK("init:zero_cap",  uk_venus_encoder_init(&enc, buf, 0) < 0);
	CHECK("init:ok",        uk_venus_encoder_init(&enc, buf, 64) == 0);
	CHECK("init:size_zero", uk_venus_encoder_size(&enc) == 0);
	CHECK("init:no_overflow", !uk_venus_encoder_overflow(&enc));
}

static void test_encoder_overflow(void)
{
	uint8_t buf[4];
	struct uk_venus_encoder enc;

	uk_venus_encoder_init(&enc, buf, 4);
	uk_venus_encode_uint32(&enc, 0xDEADBEEFu);
	CHECK("overflow:before",  !uk_venus_encoder_overflow(&enc));
	CHECK("overflow:size4",    uk_venus_encoder_size(&enc) == 4);
	uk_venus_encode_uint32(&enc, 1u); /* should overflow */
	CHECK("overflow:flag_set", uk_venus_encoder_overflow(&enc));

	/* after overflow, size should not advance */
	size_t s = uk_venus_encoder_size(&enc);
	uk_venus_encode_uint32(&enc, 2u);
	CHECK("overflow:size_frozen", uk_venus_encoder_size(&enc) == s);

	uk_venus_encoder_reset(&enc);
	CHECK("reset:overflow_cleared", !uk_venus_encoder_overflow(&enc));
	CHECK("reset:size_zero", uk_venus_encoder_size(&enc) == 0);
}

/* -------------------------------------------------- primitive field encoding */

static void test_encode_primitives(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder enc;

	uk_venus_encoder_init(&enc, buf, sizeof(buf));

	/* uint32 */
	uk_venus_encode_uint32(&enc, 0x12345678u);
	CHECK("uint32:val",    read_u32(buf) == 0x12345678u);
	CHECK("uint32:size",   uk_venus_encoder_size(&enc) == 4);

	/* Venus is PACKED — uint64 written at current pos with no padding */
	uk_venus_encode_uint64(&enc, 0xCAFEBABEDEADBEEFull);
	CHECK("uint64:packed", uk_venus_encoder_size(&enc) == 12); /* 4+8, no pad */
	CHECK("uint64:val",    read_u64(buf + 4) == 0xCAFEBABEDEADBEEFull);

	/* cstring: "ab" → [uint64:3, 'a','b','\0', pad] (Mesa vn_encode_blob_array pads to 4B) */
	uk_venus_encoder_reset(&enc);
	uk_venus_encode_cstring(&enc, "ab");
	/* uint64 count (8B) + chars (3B) + 1 pad byte = 12B total */
	CHECK("cstring:ab_size",  uk_venus_encoder_size(&enc) == 12);
	CHECK("cstring:ab_len",   read_u64(buf) == 3u);
	CHECK("cstring:ab_chars", buf[8] == 'a' && buf[9] == 'b' && buf[10] == '\0');

	/* null cstring → [uint64:0] */
	uk_venus_encoder_reset(&enc);
	uk_venus_encode_cstring(&enc, NULL);
	CHECK("cstring:null_size", uk_venus_encoder_size(&enc) == 8);
	CHECK("cstring:null_zero", read_u64(buf) == 0u);

	/* pointer flag */
	uk_venus_encoder_reset(&enc);
	uk_venus_encode_pointer_flag(&enc, 1);
	uk_venus_encode_pointer_flag(&enc, 0);
	CHECK("ptr:present", read_u64(buf)     == VN_PTR_PRESENT);
	CHECK("ptr:null",    read_u64(buf + 8) == VN_PTR_NULL);
}

/* ------------------------------------------------- vkCreateInstance encoding */

static void test_encode_create_instance(void)
{
	uint8_t buf[512];
	struct uk_venus_encoder enc;

	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkCreateInstance(&enc,
					 0xABCD0001ull, /* instance handle */
					 "unikraft-test",
					 0x00401000u,   /* VK_API_VERSION_1_1 */
					 0, NULL);

	CHECK("create_inst:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_inst:nonzero_size", uk_venus_encoder_size(&enc) > 0);

	/* First 8 bytes are VkCommandTypeEXT + VkCommandFlagsEXT. */
	CHECK("create_inst:cmd_type", read_u32(buf) == (uint32_t)VN_CMD_vkCreateInstance);
	CHECK("create_inst:flags", read_u32(buf + 4) == VN_COMMAND_FLAGS_NONE);

	/* pCreateInfo is a uint64_t array-size/pointer-present marker. */
	CHECK("create_inst:ptr_present", read_u64(buf + 8) == VN_PTR_PRESENT);

	/* sType of VkInstanceCreateInfo = 1 */
	CHECK("create_inst:stype", read_u32(buf + 16) == 1u);
}

/* ----------------------------------------- vkEnumeratePhysicalDevices encoding */

static void test_encode_enumerate_devices(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder enc;
	uint64_t handles[2] = { 0x100000001ull, 0x100000002ull };

	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkEnumeratePhysicalDevices(&enc,
						   0xABCD0001ull,
						   2u, handles);

	CHECK("enum_devs:no_overflow",  !uk_venus_encoder_overflow(&enc));
	CHECK("enum_devs:cmd_type",
	      read_u32(buf) == (uint32_t)VN_CMD_vkEnumeratePhysicalDevices);
	CHECK("enum_devs:flags", read_u32(buf + 4) == VN_COMMAND_FLAGS_NONE);
	CHECK("enum_devs:nonzero_size", uk_venus_encoder_size(&enc) > 0);
}


static void test_encode_device_queue_submit(void)
{
	uint8_t buf[1024];
	struct uk_venus_encoder enc;
	const char *exts[] = { "VK_KHR_swapchain" };

	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkCreateDevice(&enc,
					0x200000001ull,
					0x300000001ull,
					0u, 1.0f, 1, exts);
	CHECK("create_dev:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("create_dev:cmd_type", read_u32(buf) == (uint32_t)VN_CMD_vkCreateDevice);
	CHECK("create_dev:flags", read_u32(buf + 4) == VN_COMMAND_FLAGS_NONE);
	CHECK("create_dev:phys_handle", read_u64(buf + 8) == 0x200000001ull);
	CHECK("create_dev:nonzero_size", uk_venus_encoder_size(&enc) > 0);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkGetDeviceQueue(&enc, 0x300000001ull, 0, 0,
					  0x400000001ull);
	CHECK("get_queue:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("get_queue:cmd_type", read_u32(buf) == (uint32_t)VN_CMD_vkGetDeviceQueue);
	CHECK("get_queue:device", read_u64(buf + 8) == 0x300000001ull);
	CHECK("get_queue:qfi", read_u32(buf + 16) == 0u);

	uk_venus_encoder_reset(&enc);
	uk_venus_encode_vkQueueSubmit_empty(&enc, 0x400000001ull, 0);
	CHECK("queue_submit_empty:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("queue_submit_empty:cmd_type", read_u32(buf) == (uint32_t)VN_CMD_vkQueueSubmit);
	CHECK("queue_submit_empty:queue", read_u64(buf + 8) == 0x400000001ull);
}

/* ------------------------------------------- probe/capset without real device */

static void test_probe_no_device(void)
{
	const char *s = uk_venus_probe(NULL);
	CHECK("probe:null_dev_nonull", s != NULL);
	CHECK("probe:null_dev_blocked", strstr(s, "blocked") != NULL);
}

static void test_capset_no_device(void)
{
	int rc = uk_venus_capset_get(NULL, NULL);
	CHECK("capset:null_dev", rc < 0);
}

/* ----------------------------------- device-level tests (fake VirtIO-GPU dev) */

static void test_venus_with_fake_device(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_venus_caps caps;
	int rc;

	/* Use the fake VirtIO-GPU probe to get a simulated device. */
	rc = uk_virtio_gpu_probe(&dev);
	CHECK("fakedev:probe_ok", rc == 0 && dev != NULL);
	if (!dev) return;

	/* Venus capset should be in the fake device's capset list. */
	rc = uk_venus_capset_get(dev, &caps);
	CHECK("fakedev:capset_get_ok",        rc == 0);
	CHECK("fakedev:wire_format_ver",       caps.wire_format_version >= 1);

	/* uk_venus_probe should return "pass" with a working fake device. */
	const char *status = uk_venus_probe(dev);
	CHECK("fakedev:probe_pass",  strstr(status, "pass") != NULL);

	/* Create a Venus context. */
	uint32_t ctx_id = 0;
	rc = uk_venus_context_create(dev, &ctx_id);
	CHECK("fakedev:ctx_create_ok",   rc == 0);
	CHECK("fakedev:ctx_id_nonzero",  ctx_id != 0);

	/* Submit a small encoded Venus command via SUBMIT_3D. */
	uint8_t buf[512];
	struct uk_venus_encoder enc;
	struct uk_virtio_gpu_context ctx = { .id = ctx_id, .capset_id = UK_VENUS_CAPSET_ID, .created = 1 };

	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkCreateInstance(&enc, 0x1000000Aull,
					 "unikraft", 0x00400000u, 0, NULL);
	CHECK("fakedev:enc_no_overflow", !uk_venus_encoder_overflow(&enc));

	rc = uk_venus_submit(dev, &ctx, &enc);
	CHECK("fakedev:submit_ok",   rc == 0);

	free(dev);
}


static void test_venus_ring_with_fake_device(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_venus_ring ring = { 0 };
	struct uk_virtio_gpu_metrics metrics;
	uint8_t buf[512];
	struct uk_venus_encoder enc;
	uk_gpu_fence_id fence = 0;
	size_t off = 0;
	int rc;

	rc = uk_virtio_gpu_probe(&dev);
	CHECK("ring:probe_ok", rc == 0 && dev != NULL);
	if (!dev)
		return;

	CHECK("ring:status_pass", strcmp(uk_venus_ring_status(dev), "pass") == 0);
	CHECK("ring:create_null_dev", uk_venus_ring_create(NULL, &ring, 4096, 1) < 0);
	CHECK("ring:create_zero_size", uk_venus_ring_create(dev, &ring, 0, 1) < 0);

	rc = uk_venus_ring_create(dev, &ring, 4096, 0xabc0001ull);
	CHECK("ring:create_ok", rc == 0);
	CHECK("ring:ready", ring.ready && ring.base != NULL && ring.size == 4096);
	CHECK("ring:ctx_blob_live", ring.ctx.created && ring.blob.created && ring.blob.mapped);

	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkQueueSubmit_empty(&enc, 0x400000001ull, 0);
	CHECK("ring:enc_ready", !uk_venus_encoder_overflow(&enc) && uk_venus_encoder_size(&enc) > 0);

	rc = uk_venus_ring_submit(dev, &ring, &enc, &fence);
	CHECK("ring:submit_ok", rc == 0);
	CHECK("ring:fence_nonzero", fence != 0);
	CHECK("ring:counters", ring.commands_submitted == 1 && ring.bytes_written == uk_venus_encoder_size(&enc));
	CHECK("ring:payload_copied", memcmp(ring.base, buf, uk_venus_encoder_size(&enc)) == 0);

	uint8_t sample[16] = { 1, 2, 3, 4 };
	rc = uk_venus_ring_write(&ring, sample, sizeof(sample), &off);
	CHECK("ring:write_ok", rc == 0 && off == uk_venus_encoder_size(&enc));
	CHECK("ring:write_copied", memcmp(ring.base + off, sample, sizeof(sample)) == 0);

	uint8_t too_big[4096];
	memset(too_big, 0xa5, sizeof(too_big));
	CHECK("ring:overflow_enospc", uk_venus_ring_write(&ring, too_big, sizeof(too_big), NULL) == -ENOSPC);

	memset(&metrics, 0, sizeof(metrics));
	rc = uk_virtio_gpu_gl_metrics_get(dev, &metrics);
	CHECK("ring:metrics_get", rc == 0);
	CHECK("ring:metrics_blob", metrics.blobs_created >= 1 && metrics.blobs_mapped >= 1);
	CHECK("ring:metrics_ctx_submit", metrics.contexts_created >= 1 && metrics.submits_3d >= 1);

	uk_venus_ring_destroy(dev, &ring);
	CHECK("ring:destroy_zeroes", !ring.ready && !ring.ctx.created && !ring.blob.created);
	free(dev);
}

static void test_venus_ring_perf_probe(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_venus_ring ring = { 0 };
	uint8_t payload[256];
	struct timespec t0, t1;
	const int iters = 256;
	uint64_t ns;
	int rc;

	rc = uk_virtio_gpu_probe(&dev);
	CHECK("ring_perf:probe_ok", rc == 0 && dev != NULL);
	if (!dev)
		return;
	rc = uk_venus_ring_create(dev, &ring, UK_VENUS_RING_DEFAULT_SIZE, 0xabc0002ull);
	CHECK("ring_perf:create_ok", rc == 0);
	if (rc) {
		free(dev);
		return;
	}
	memset(payload, 0x5a, sizeof(payload));
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < iters; i++) {
		rc = uk_venus_ring_write(&ring, payload, sizeof(payload), NULL);
		if (rc)
			break;
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
	     (uint64_t)(t1.tv_nsec - t0.tv_nsec);
	CHECK("ring_perf:writes_ok", rc == 0);
	printf("venus_ring_perf: bytes=%llu writes=%d elapsed_ns=%llu throughput_mib_s=%.2f\n",
	       (unsigned long long)ring.bytes_written, iters,
	       (unsigned long long)ns,
	       ns ? ((double)ring.bytes_written * 1000000000.0 / (double)ns / 1048576.0) : 0.0);
	uk_venus_ring_destroy(dev, &ring);
	free(dev);
}

/* ----------------------------------------- vkCreateRingMESA encoding test */

static void test_encode_ring_transport(void)
{
	uint8_t buf[256];
	struct uk_venus_encoder enc;

	/* vkCreateRingMESA */
	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkCreateRingMESA(&enc,
					  0xBEEF0001ull,  /* ring_id */
					  0x1234u,        /* resource_id */
					  65728ull,       /* blob_size = 192+65536 */
					  65536ull);      /* buf_size */
	CHECK("create_ring:no_overflow",   !uk_venus_encoder_overflow(&enc));
	CHECK("create_ring:nonzero_size",  uk_venus_encoder_size(&enc) > 0);
	CHECK("create_ring:cmd_type",
	      read_u32(buf) == (uint32_t)VN_CMD_vkCreateRingMESA);
	CHECK("create_ring:flags",  read_u32(buf + 4) == VN_COMMAND_FLAGS_NONE);
	CHECK("create_ring:ring_id", read_u64(buf + 8) == 0xBEEF0001ull);
	/* Pointer flag at buf+16 = 1 (pCreateInfo present) */
	CHECK("create_ring:ptr_present", read_u64(buf + 16) == VN_PTR_PRESENT);
	/* sType at buf+24 = 1000384000 */
	CHECK("create_ring:stype",
	      read_u32(buf + 24) == (uint32_t)VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA);

	/* vkDestroyRingMESA */
	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkDestroyRingMESA(&enc, 0xBEEF0001ull);
	CHECK("destroy_ring:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("destroy_ring:cmd_type",
	      read_u32(buf) == (uint32_t)VN_CMD_vkDestroyRingMESA);
	CHECK("destroy_ring:ring_id", read_u64(buf + 8) == 0xBEEF0001ull);

	/* vkNotifyRingMESA */
	uk_venus_encoder_init(&enc, buf, sizeof(buf));
	uk_venus_encode_vkNotifyRingMESA(&enc, 0xBEEF0001ull, 42u);
	CHECK("notify_ring:no_overflow", !uk_venus_encoder_overflow(&enc));
	CHECK("notify_ring:cmd_type",
	      read_u32(buf) == (uint32_t)VN_CMD_vkNotifyRingMESA);
	CHECK("notify_ring:ring_id", read_u64(buf + 8) == 0xBEEF0001ull);
	/* seqno at buf+16 = 42 */
	CHECK("notify_ring:seqno", read_u32(buf + 16) == 42u);
}

/* --------------------------------- full Venus ring protocol (fake device) */

static void test_venus_ring_protocol(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_venus_ring ring = { 0 };
	uint8_t enc_buf[512];
	struct uk_venus_encoder enc;
	volatile uint32_t *head_ptr;
	volatile uint32_t *tail_ptr;
	int rc;

	rc = uk_virtio_gpu_probe(&dev);
	CHECK("ring_proto:probe_ok", rc == 0 && dev != NULL);
	if (!dev)
		return;

	/*
	 * Create the ring blob with size = UK_VENUS_RING_CTRL_SIZE + 4096.
	 * The buf_size will be venus_prev_pow2(4096) = 4096 (power of 2).
	 */
	rc = uk_venus_ring_create(dev, &ring,
				  UK_VENUS_RING_BUFFER_OFFSET + 4096u,
				  0xBEEF0001ull);
	CHECK("ring_proto:create_ok", rc == 0);
	if (rc) { free(dev); return; }
	CHECK("ring_proto:size_correct",
	      ring.size == UK_VENUS_RING_BUFFER_OFFSET + 4096u);

	/* uk_venus_ring_register: initializes head/tail/status, sends vkCreateRingMESA. */
	rc = uk_venus_ring_register(dev, &ring, 0xDEAD0001ull);
	CHECK("ring_proto:register_ok",   rc == 0);
	CHECK("ring_proto:protocol_ready", ring.protocol_ready);
	CHECK("ring_proto:buf_size",  ring.buf_size == 4096u);
	CHECK("ring_proto:buf_mask",  ring.buf_mask == 4095u);
	CHECK("ring_proto:ring_id",   ring.ring_id == 0xDEAD0001ull);
	CHECK("ring_proto:cur_tail",  ring.cur_tail == 0);

	/* Control fields in shared memory must be zero after register. */
	head_ptr = (volatile uint32_t *)(ring.base + UK_VENUS_RING_HEAD_OFFSET);
	tail_ptr = (volatile uint32_t *)(ring.base + UK_VENUS_RING_TAIL_OFFSET);
	CHECK("ring_proto:head_zero", *head_ptr == 0);
	CHECK("ring_proto:tail_zero", *tail_ptr == 0);

	/* Write a small Venus command to the circular buffer. */
	uk_venus_encoder_init(&enc, enc_buf, sizeof(enc_buf));
	uk_venus_encode_vkQueueSubmit_empty(&enc, 0x400000001ull, 0);
	CHECK("ring_proto:enc_ok", !enc.overflow);

	rc = uk_venus_ring_cmd_write(&ring, enc_buf, (uint32_t)enc.pos);
	CHECK("ring_proto:cmd_write_ok",    rc == 0);
	CHECK("ring_proto:cur_tail_adv",    ring.cur_tail == (uint32_t)enc.pos);

	/* Verify data landed in the circular buffer at offset 0 (head was 0). */
	CHECK("ring_proto:data_correct",
	      memcmp(ring.base + UK_VENUS_RING_BUFFER_OFFSET, enc_buf, enc.pos) == 0);

	/* Flush: stores tail to shared memory, sends vkNotifyRingMESA. */
	rc = uk_venus_ring_cmd_flush(dev, &ring);
	CHECK("ring_proto:flush_ok",  rc == 0);
	CHECK("ring_proto:tail_stored", *tail_ptr == ring.cur_tail);

	/* For the native fake device: simulate host processing by advancing head. */
	*head_ptr = ring.cur_tail;
	CHECK("ring_proto:load_head", uk_venus_ring_load_head(&ring) == ring.cur_tail);

	/* Wait should resolve immediately (head already == cur_tail). */
	rc = uk_venus_ring_cmd_wait(&ring, 1u);
	CHECK("ring_proto:wait_ok", rc == 0);

	/* Test wrap-around: fill exactly to the buffer end, then write at offset 0. */
	{
		uint8_t fill[32];
		uint32_t first_write = ring.cur_tail;
		memset(fill, 0xAB, sizeof(fill));
		/* Advance cur_tail to near the buffer end. */
		ring.cur_tail = ring.buf_size - (uint32_t)sizeof(fill) / 2;
		*head_ptr     = ring.cur_tail; /* keep space available */
		rc = uk_venus_ring_cmd_write(&ring, fill, sizeof(fill));
		CHECK("ring_proto:wrap_write_ok", rc == 0);
		/* Data wraps: first half at end of buffer, rest at start. */
		(void)first_write;
	}

	/* Unregister: sends vkDestroyRingMESA. */
	rc = uk_venus_ring_unregister(dev, &ring);
	CHECK("ring_proto:unregister_ok",       rc == 0);
	CHECK("ring_proto:protocol_cleared",    !ring.protocol_ready);
	CHECK("ring_proto:ring_id_cleared",     ring.ring_id == 0);

	/* Double-unregister is safe. */
	rc = uk_venus_ring_unregister(dev, &ring);
	CHECK("ring_proto:double_unregister_ok", rc == 0);

	uk_venus_ring_destroy(dev, &ring);
	free(dev);
}

/* ---------------------------------------------------------------------- main */

int main(void)
{
	printf("venus_cs_test:\n");

	test_encoder_init();
	test_encoder_overflow();
	test_encode_primitives();
	test_encode_create_instance();
	test_encode_enumerate_devices();
	test_encode_device_queue_submit();
	test_encode_ring_transport();
	test_probe_no_device();
	test_capset_no_device();
	test_venus_with_fake_device();
	test_venus_ring_with_fake_device();
	test_venus_ring_protocol();
	test_venus_ring_perf_probe();

	if (g_fail_count) {
		printf("venus_cs_test: %d check(s) FAILED\n", g_fail_count);
		return 1;
	}
	printf("venus_cs_test: all checks passed\n");
	return 0;
}
