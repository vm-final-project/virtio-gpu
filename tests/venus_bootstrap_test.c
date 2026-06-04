/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * venus_bootstrap_test — native Venus Vulkan driver bootstrap test.
 *
 * Exercises the libukvulkan_venus driver-open path against the fake VirtIO-GPU
 * backend (no Linux virtgpu DRM UAPI). Validates:
 *   - open via uk_vulkan_venus_open() (native probe + Venus context)
 *   - device info: device name, capset id, venus_available
 *   - Venus capset detection (id=4, bit 4 of supported_capsets)
 *   - has_resource_blob, has_context_init flags
 *   - rendering_status == "blocked:ring-buffer-frame-proof-missing"
 *   - idempotent uk_vulkan_venus_get_info()
 *   - error handling: NULL dev, uninitialised dev, NULL args
 *   - uk_vulkan_venus_close() zeroes state
 *
 * Build: see tests/Makefile (native target)
 * Run:   ./build/venus_bootstrap_test
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <uk/vulkan_venus.h>
#include <uk/virtio_gpu.h>

#define PASS(label)   do { printf("  PASS  %s\n", label); } while (0)
#define FAIL(label, rc) do { \
    printf("  FAIL  %s  rc=%d\n", label, rc); \
    return 1; \
} while (0)
#define CHECK(label, expr) do { \
    int _rc = (expr); \
    if (_rc != 0) FAIL(label, _rc); \
    PASS(label); \
} while (0)
#define CHECK_NZ(label, val) do { \
    if (!(val)) FAIL(label, 0); \
    PASS(label); \
} while (0)
#define CHECK_STR(label, got, want) do { \
    if (strcmp((got), (want)) != 0) { \
        printf("  FAIL  %s  got='%s' want='%s'\n", label, got, want); \
        return 1; \
    } \
    PASS(label); \
} while (0)

int main(void)
{
    struct uk_vulkan_venus_dev venus;
    struct uk_vulkan_venus_info info;
    int rc;

    printf("venus_bootstrap_test: native Venus driver against fake VirtIO-GPU backend\n");

    /* ── Error handling: NULL dev ────────────────────────────────────────── */
    rc = uk_vulkan_venus_open(NULL, 0);
    if (rc != -EINVAL) FAIL("open:null_dev returns EINVAL", rc);
    PASS("open:null_dev returns EINVAL");

    /* ── Open ────────────────────────────────────────────────────────────── */
    CHECK("open", uk_vulkan_venus_open(&venus, 0));
    CHECK_NZ("open:opened", venus.opened);
    CHECK_NZ("open:gpu non-null", uk_vulkan_venus_gpu(&venus));

    /* ── Device info: device name ────────────────────────────────────────── */
    CHECK_STR("info:device_name", venus.info.device_name, "virtio-gpu-venus");

    /* ── Device info: capset ─────────────────────────────────────────────── */
    if (venus.info.capset_id != UK_VIRTIO_GPU_CAPSET_VENUS)
        FAIL("info:capset_id == VENUS(4)", (int)venus.info.capset_id);
    PASS("info:capset_id == VENUS(4)");

    /* ── Device info: Venus available ────────────────────────────────────── */
    CHECK_NZ("info:venus_available", venus.info.venus_available);

    /* ── Device info: supported_capsets has Venus bit ────────────────────── */
    CHECK_NZ("info:supported_capsets has_bit4",
             venus.info.supported_capsets & (1ull << UK_VIRTIO_GPU_CAPSET_VENUS));

    /* ── Device info: capability flags ──────────────────────────────────── */
    CHECK_NZ("info:has_resource_blob", venus.info.has_resource_blob);
    CHECK_NZ("info:has_context_init",  venus.info.has_context_init);

    /* ── Rendering status is explicitly blocked ──────────────────────────── */
    if (!venus.info.rendering_status ||
        strcmp(venus.info.rendering_status, "blocked:ring-buffer-frame-proof-missing") != 0) {
        printf("  FAIL  info:rendering_status  got='%s'\n",
               venus.info.rendering_status ? venus.info.rendering_status : "(null)");
        return 1;
    }
    PASS("info:rendering_status == blocked:ring-buffer-frame-proof-missing");

    /* ── get_info ────────────────────────────────────────────────────────── */
    memset(&info, 0, sizeof(info));
    CHECK("get_info", uk_vulkan_venus_get_info(&venus, &info));
    CHECK_STR("get_info:device_name", info.device_name, "virtio-gpu-venus");
    if (info.capset_id != UK_VIRTIO_GPU_CAPSET_VENUS)
        FAIL("get_info:capset_id", (int)info.capset_id);
    PASS("get_info:capset_id");
    CHECK_NZ("get_info:venus_available", info.venus_available);

    /* ── get_info: idempotent ────────────────────────────────────────────── */
    {
        struct uk_vulkan_venus_info info2;
        memset(&info2, 0, sizeof(info2));
        CHECK("get_info:idempotent", uk_vulkan_venus_get_info(&venus, &info2));
        if (info2.capset_id != info.capset_id ||
            info2.venus_available != info.venus_available)
            FAIL("get_info:idempotent:same_result", 0);
        PASS("get_info:idempotent:same_result");
    }

    /* ── Error: get_info on uninit'd dev ─────────────────────────────────── */
    {
        struct uk_vulkan_venus_dev dev2;
        memset(&dev2, 0, sizeof(dev2));
        rc = uk_vulkan_venus_get_info(&dev2, &info);
        if (rc != -EINVAL) FAIL("get_info:uninit returns EINVAL", rc);
        PASS("get_info:uninit returns EINVAL");
    }

    /* ── Error: get_info NULL args ───────────────────────────────────────── */
    rc = uk_vulkan_venus_get_info(NULL, &info);
    if (rc != -EINVAL) FAIL("get_info:null_dev returns EINVAL", rc);
    PASS("get_info:null_dev returns EINVAL");

    rc = uk_vulkan_venus_get_info(&venus, NULL);
    if (rc != -EINVAL) FAIL("get_info:null_info returns EINVAL", rc);
    PASS("get_info:null_info returns EINVAL");

    /* ── capset_name helper ──────────────────────────────────────────────── */
    {
        const char *name = uk_virtio_gpu_capset_name(UK_VIRTIO_GPU_CAPSET_VENUS);
        CHECK_NZ("capset_name:venus non-null", name);
        CHECK_NZ("capset_name:venus non-empty", name[0]);
    }

    /* ── Venus context created natively ──────────────────────────────────── */
    CHECK_NZ("ctx_ready", venus.ctx_ready);
    if (uk_vulkan_venus_ctx(&venus)->capset_id != UK_VIRTIO_GPU_CAPSET_VENUS)
        FAIL("ctx:capset_id == VENUS", (int)uk_vulkan_venus_ctx(&venus)->capset_id);
    PASS("ctx:capset_id == VENUS");

    /* ── close zeroes state ──────────────────────────────────────────────── */
    uk_vulkan_venus_close(&venus);
    if (venus.opened || venus.gpu || venus.ctx_ready)
        FAIL("close:zeroed_state", 0);
    PASS("close:zeroed_state");

    /* ── close on NULL is safe ───────────────────────────────────────────── */
    uk_vulkan_venus_close(NULL);
    PASS("close:null_safe");

    printf("venus_bootstrap_test: all checks passed\n");
    return 0;
}
