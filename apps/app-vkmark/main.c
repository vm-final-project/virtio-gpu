/*
 * app-vkmark — gfx.vkmark Vulkan benchmark substrate port.
 *
 * Initialises the libukvk_icd Vulkan ICD over libukvirtgpu_drm, enumerates the
 * vkmark scene list, and prints `pass-substrate`. Scene fps requires a real
 * Venus render payload and is intentionally not claimed here. Upstream
 * vkmark: https://github.com/vkmark/vkmark (LGPL-2.1+).
 */
#include <stdint.h>
#include <stdio.h>

#include <uk/vulkan_icd.h>

#define VKMARK_BLOCK(msg, ...) \
    do { printf("uk-vkmark: BLOCKED " msg "\n", ##__VA_ARGS__); return 0; } while (0)

static const char * const vkmark_scenes[] = {
    "vertex", "texture", "shading", "desktop",
    "effect2d", "terrain", "shadow", "refract",
    "compute", "clear",
    NULL,
};

int main(void)
{
    printf("uk-vkmark: booted vkmark Vulkan benchmark substrate port\n");

    printf("uk-vkmark: scenes=");
    for (int i = 0; vkmark_scenes[i]; i++)
        printf("%s%s", i ? "," : "", vkmark_scenes[i]);
    printf("\n");

    struct uk_vulkan_icd_dev icd;
    int rc = uk_vulkan_icd_init(&icd, 0);
    if (rc) {
        printf("uk-vkmark: g6=blocked:icd-init-failed rc=%d\n", rc);
        VKMARK_BLOCK("vk.icd Vulkan ICD init failed; requires virtio-gpu-gl-pci");
    }

    struct uk_vulkan_icd_info info;
    uk_vulkan_icd_get_device_info(&icd, &info);

    printf("uk-vkmark: g5=pass g6=pass\n");
    printf("uk-vkmark: device=%s capset_id=%u venus=%d blob=%d\n",
           info.device_name, info.capset_id,
           info.venus_available, info.has_resource_blob);
    printf("uk-vkmark: ctx_initialized=%d rendering_status=%s\n",
           icd.ctx_initialized, info.rendering_status);

    int n = 0;
    for (int i = 0; vkmark_scenes[i]; i++) {
        printf("uk-vkmark: scene=%s status=%s\n",
               vkmark_scenes[i], info.rendering_status);
        n++;
    }

    printf("uk-vkmark: substrate=pass scenes=%d status=pass-substrate\n", n);
    printf("uk-vkmark: next_step=implement-venus-command-encoder\n");

    uk_vulkan_icd_close(&icd);
    return 0;
}
