/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <string.h>

#include <uk/venus.h>

struct test_state {
	int checks;
	int failed;
};

static void check(struct test_state *t, const char *name, int ok)
{
	t->checks++;
	if (!ok) {
		t->failed++;
		printf("  FAIL  %s\n", name);
	} else {
		printf("  PASS  %s\n", name);
	}
}

int main(void)
{
	struct test_state t = { 0 };
	struct uk_venus_caps caps;
	uint32_t short_raw[5] = { 1, 352, 2, 1, 1 };
	uint32_t full[40] = { 0 };

	uk_venus_decode_capset(&caps, (const uint8_t *)short_raw,
			       sizeof(short_raw));
	check(&t, "short wire version", caps.wire_format_version == 1);
	check(&t, "short xml version", caps.vk_xml_version == 352);
	check(&t, "short supports blob id 0", caps.supports_blob_id_0 == 1);
	check(&t, "short no extension mask", caps.vk_extension_mask1[0] == 0);
	check(&t, "short no wait syncs", caps.allow_vk_wait_syncs == 0);

	full[0] = 1;
	full[4] = 1;
	full[5] = 0x80000001u;
	full[37] = 1;
	full[38] = 1;
	full[39] = 1;

	memset(&caps, 0, sizeof(caps));
	uk_venus_decode_capset(&caps, (const uint8_t *)full, sizeof(full));
	check(&t, "full wire version", caps.wire_format_version == 1);
	check(&t, "full supports blob id 0", caps.supports_blob_id_0 == 1);
	check(&t, "full extension mask", caps.vk_extension_mask1[0] == 0x80000001u);
	check(&t, "full wait syncs", caps.allow_vk_wait_syncs == 1);
	check(&t, "full timelines", caps.supports_multiple_timelines == 1);
	check(&t, "full guest vram", caps.use_guest_vram == 1);

	printf("venus_capset: %s checks=%d\n",
	       t.failed ? "FAIL" : "PASS", t.checks);
	return t.failed ? 1 : 0;
}
