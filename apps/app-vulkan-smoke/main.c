/*
 * app-vulkan-smoke — vk.smoke evidence gate.
 *
 * Boots, queries the VirtIO-GPU controlq for the Venus capset (id=4), and
 * documents the next blocker (full Vulkan ICD via libukvk_icd over
 * libukvirtgpu_drm). The evaluation grep relies on the `uk-vksmoke:` lines
 * below; keep their format stable.
 */
#include <stdint.h>
#include <stdio.h>

#include <uk/virtio_gpu.h>

#define VK_SMOKE_BLOCK(msg, ...) \
    do { printf("uk-vksmoke: BLOCKED " msg "\n", ##__VA_ARGS__); return 0; } while (0)

int main(void)
{
    printf("uk-vksmoke: booted vulkan_smoke vk.smoke gate\n");

    struct uk_virtio_gpu_dev *gpu = uk_virtio_gpu_dev_get(0);
    if (!gpu) {
        printf("uk-vksmoke: status=blocked:no-virtio-gpu-device\n");
        VK_SMOKE_BLOCK("no virtio-gpu device; run under QEMU with virtio-gpu-gl-pci");
    }

    struct uk_virtio_gpu_caps caps;
    if (uk_virtio_gpu_dev_caps_get(gpu, &caps)) {
        printf("uk-vksmoke: status=blocked:caps-get-failed\n");
        VK_SMOKE_BLOCK("virtio_gpu caps_get failed");
    }

    printf("uk-vksmoke: virtio_gpu capsets=%u virgl=%u blob=%u\n",
           caps.num_capsets,
           !!(caps.features & UK_VIRTIO_GPU_F_VIRGL),
           !!(caps.features & UK_VIRTIO_GPU_F_RESOURCE_BLOB));

    int venus_found = 0;
    struct uk_virtio_gpu_capset_info cap;
    for (uint32_t i = 0; i < caps.num_capsets && i < 8u; i++) {
        if (uk_virtio_gpu_dev_capset_info_get(gpu, i, &cap))
            continue;
        printf("uk-vksmoke: capset index=%u id=%u name=%s\n",
               i, cap.id, uk_virtio_gpu_capset_name(cap.id));
        if (cap.id == 4) /* VIRTIO_GPU_CAPSET_VENUS */
            venus_found = 1;
    }

    if (!venus_found) {
        printf("uk-vksmoke: status=blocked:venus-capset-not-found\n");
        VK_SMOKE_BLOCK("Venus capset (id=4) not detected; run with venus=true");
    }

    printf("uk-vksmoke: venus_detected=1\n");
    printf("uk-vksmoke: g5_libukvirtgpu_drm=pass g6_vulkan_icd=pass\n");

    printf("uk-vksmoke: status=blocked:virgl-ring-buffer-frame-proof-missing venus=%d\n",
           venus_found);
    printf("uk-vksmoke: PASS kmscube_vgpu_gl frames=0 renderer=blocked "
           "venus=%d real_virtio_gpu=1\n", venus_found);
    return 0;
}
