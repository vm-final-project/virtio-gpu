/*
 * vulkan_compute_test.c — headless Vulkan API surface baseline for VOGUE
 *
 * Validates Vulkan device enumeration, queue families, buffer allocation,
 * memory mapping, and fence synchronization on the host. Used as reference
 * baseline for Venus/GPU acceleration comparison. No window system required.
 *
 * Based on Khronos Vulkan Samples API patterns (hello_triangle, dynamic_buffers).
 * See /home/jerrytsai/Vulkan-Samples/samples/api/
 *
 * Build: see tests/Makefile (vulkan target)
 * Run:   ./build/vulkan_compute_test
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vulkan/vulkan.h>

#define VK_CHECK(expr)                                                          \
    do {                                                                        \
        VkResult _r = (expr);                                                   \
        if (_r != VK_SUCCESS) {                                                 \
            fprintf(stderr, "VK_CHECK failed: %s = %d at %s:%d\n",             \
                    #expr, _r, __FILE__, __LINE__);                             \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *mp,
                                  uint32_t mask, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((mask & (1u << i)) && (mp->memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fprintf(stderr, "find_memory_type: no suitable type\n");
    exit(1);
}

int main(void)
{
    printf("vulkan_compute_test: Vulkan API surface baseline\n");
    printf("vulkan_compute_test: reference Khronos Vulkan-Samples=%s\n",
           "/home/jerrytsai/Vulkan-Samples");

    /* Instance */
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.pApplicationName   = "vogue-vulkan-baseline";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion         = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &ai;
    VkInstance inst;
    VK_CHECK(vkCreateInstance(&ici, NULL, &inst));

    /* Physical device enumeration */
    uint32_t pdev_count = 0;
    vkEnumeratePhysicalDevices(inst, &pdev_count, NULL);
    if (pdev_count == 0) {
        fprintf(stderr, "vulkan_compute_test: no Vulkan devices\n");
        return 1;
    }
    VkPhysicalDevice *pdevs = calloc(pdev_count, sizeof(*pdevs));
    vkEnumeratePhysicalDevices(inst, &pdev_count, pdevs);

    printf("vulkan_compute_test: physical_devices=%u\n", pdev_count);

    /* Report all devices, prefer CPU device (llvmpipe) for deterministic baseline */
    VkPhysicalDevice pdev = pdevs[0];
    VkPhysicalDeviceProperties pdev_props;
    for (uint32_t i = 0; i < pdev_count; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pdevs[i], &p);
        static const char *type_name[] = { "other","integrated","discrete","virtual","cpu" };
        printf("vulkan_compute_test:   gpu%u=%s type=%s api=%u.%u\n",
               i, p.deviceName,
               p.deviceType < 5 ? type_name[p.deviceType] : "?",
               VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion));
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            pdev = pdevs[i]; /* prefer llvmpipe for reproducible baseline */
    }

    vkGetPhysicalDeviceProperties(pdev, &pdev_props);
    printf("vulkan_compute_test: selected=%s\n", pdev_props.deviceName);

    /* Memory properties */
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &mem_props);
    printf("vulkan_compute_test: memory_heaps=%u memory_types=%u\n",
           mem_props.memoryHeapCount, mem_props.memoryTypeCount);

    /* Queue family with compute support */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, NULL);
    VkQueueFamilyProperties *qfps = calloc(qf_count, sizeof(*qfps));
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &qf_count, qfps);

    uint32_t compute_qf = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qfps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { compute_qf = i; break; }
    }
    if (compute_qf == UINT32_MAX) {
        fprintf(stderr, "vulkan_compute_test: no compute queue family\n");
        return 1;
    }
    printf("vulkan_compute_test: compute_queue_family=%u\n", compute_qf);

    /* Logical device */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    dqci.queueFamilyIndex = compute_qf;
    dqci.queueCount       = 1;
    dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &dqci;
    VkDevice dev;
    VK_CHECK(vkCreateDevice(pdev, &dci, NULL, &dev));

    VkQueue queue;
    vkGetDeviceQueue(dev, compute_qf, 0, &queue);

    /* ── Buffer allocation benchmark ──────────────────────────────────────── */
    const uint32_t REPS    = 32;
    const VkDeviceSize BUF = 65536; /* 64 KiB */
    uint64_t alloc_total_ns = 0;
    uint64_t map_total_ns   = 0;
    uint32_t fence_signals  = 0;

    for (uint32_t rep = 0; rep < REPS; rep++) {
        uint64_t t0 = now_ns();

        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size  = BUF;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buf;
        VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, buf, &mr);

        VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize  = mr.size;
        mai.memoryTypeIndex = find_memory_type(&mem_props, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory mem;
        VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
        VK_CHECK(vkBindBufferMemory(dev, buf, mem, 0));

        uint64_t t1 = now_ns();
        alloc_total_ns += (t1 - t0);

        /* Map, write, unmap */
        t0 = now_ns();
        void *mapped;
        VK_CHECK(vkMapMemory(dev, mem, 0, BUF, 0, &mapped));
        memset(mapped, (int)rep, (size_t)BUF);
        vkUnmapMemory(dev, mem);
        t1 = now_ns();
        map_total_ns += (t1 - t0);

        vkFreeMemory(dev, mem, NULL);
        vkDestroyBuffer(dev, buf, NULL);
    }

    /* ── Command pool and fence benchmark ─────────────────────────────────── */
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.queueFamilyIndex = compute_qf;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cpool;
    VK_CHECK(vkCreateCommandPool(dev, &cpci, NULL, &cpool));

    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool        = cpool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));

    uint64_t fence_total_ns = 0;
    for (uint32_t rep = 0; rep < REPS; rep++) {
        VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));
        VK_CHECK(vkEndCommandBuffer(cb));

        VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence fence;
        VK_CHECK(vkCreateFence(dev, &fci, NULL, &fence));

        uint64_t t0 = now_ns();
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
        VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
        uint64_t t1 = now_ns();
        fence_total_ns += (t1 - t0);
        fence_signals++;

        vkDestroyFence(dev, fence, NULL);
        VK_CHECK(vkResetCommandBuffer(cb, 0));
    }

    /* ── Report results ───────────────────────────────────────────────────── */
    printf("vulkan_compute_test: reps=%u buf_size=%u\n", REPS, (uint32_t)BUF);
    printf("vulkan_compute_test: alloc_avg_us=%llu map_avg_us=%llu fence_avg_us=%llu\n",
           (unsigned long long)(alloc_total_ns / REPS / 1000),
           (unsigned long long)(map_total_ns   / REPS / 1000),
           (unsigned long long)(fence_total_ns / REPS / 1000));
    printf("vulkan_compute_test: fence_signals=%u device=%s\n",
           fence_signals, pdev_props.deviceName);

    /* Extension count as API surface metric */
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(pdev, NULL, &ext_count, NULL);
    printf("vulkan_compute_test: device_extensions=%u\n", ext_count);

    printf("vulkan_compute_test: PASS devices=%u compute_queue=%u fence_signals=%u\n",
           pdev_count, compute_qf, fence_signals);

    /* Cleanup */
    vkFreeCommandBuffers(dev, cpool, 1, &cb);
    vkDestroyCommandPool(dev, cpool, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    free(pdevs);
    free(qfps);
    return 0;
}
