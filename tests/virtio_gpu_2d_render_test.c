#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uk/alloc.h>
#include <uk/sglist.h>
#include <uk/virtio_gpu.h>

#define WIDTH  256u
#define HEIGHT 256u
#define NFRAMES 3u
#define BPP 4u

#define FAIL_IF(cond, code) 	do { if (cond) { printf("virtio_gpu_2d_render_test FAIL code=%d line=%d\n", (code), __LINE__); return (code); } } while (0)

static uint32_t fnv1a32(const void *data, size_t n)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
	return h;
}

static void fill_pattern(uint32_t *pixels, uint32_t w, uint32_t h, uint32_t frame)
{
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint8_t r = (uint8_t)(x + frame * 17u);
			uint8_t g = (uint8_t)(y + frame * 29u);
			uint8_t b = (uint8_t)(x ^ y ^ frame);
			pixels[y * w + x] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
		}
	}
}

int main(void)
{
	struct uk_virtio_gpu_dev *dev = NULL;
	struct uk_alloc *alloc = uk_alloc_get_default();
	void *buf_vaddr = NULL;
	struct uk_sglist sg;
	struct uk_sglist_seg sg_seg[1];
	struct uk_virtio_gpu_metrics m_after;
	struct uk_gpu_rect rect = {0, 0, WIDTH, HEIGHT};
	uk_gpu_res_id res[NFRAMES];
	uk_gpu_fence_id fence;
	uint32_t crcs[NFRAMES];
	size_t buf_len = (size_t)WIDTH * HEIGHT * BPP;
	int rc;

	rc = uk_virtio_gpu_probe(&dev);
	FAIL_IF(rc != 0 || !dev, 1);
	rc = uk_virtio_gpu_gl_metrics_reset(dev);
	FAIL_IF(rc != 0, 2);

	rc = uk_posix_memalign(alloc, &buf_vaddr, 4096, buf_len);
	FAIL_IF(rc != 0 || !buf_vaddr, 4);
	uk_sglist_init(&sg, 1, sg_seg);
	rc = uk_sglist_append(&sg, buf_vaddr, buf_len);
	FAIL_IF(rc != 0 || sg.sg_nseg != 1, 5);

	for (uint32_t f = 0; f < NFRAMES; f++) {
		fill_pattern((uint32_t *)buf_vaddr, WIDTH, HEIGHT, f);
		crcs[f] = fnv1a32(buf_vaddr, buf_len);

		res[f] = 0;
		rc = uk_virtio_gpu_resource_create_2d(dev, WIDTH, HEIGHT, 1, &res[f]);
		FAIL_IF(rc != 0 || !res[f], 10 + (int)f);
		rc = uk_virtio_gpu_resource_attach_backing(dev, res[f], &sg);
		FAIL_IF(rc != 0, 20 + (int)f);

		fence = 0;
		rc = uk_virtio_gpu_transfer_to_host_2d(dev, res[f], &rect, &fence);
		FAIL_IF(rc != 0, 40 + (int)f);
		rc = uk_virtio_gpu_fence_wait(dev, fence, 1000000u);
		FAIL_IF(rc != 0, 50 + (int)f);

		rc = uk_virtio_gpu_gl_set_scanout(dev, 0, res[f], &rect);
		FAIL_IF(rc != 0, 60 + (int)f);

		fence = 0;
		rc = uk_virtio_gpu_resource_flush(dev, res[f], &rect, &fence);
		FAIL_IF(rc != 0, 70 + (int)f);
		rc = uk_virtio_gpu_fence_wait(dev, fence, 1000000u);
		FAIL_IF(rc != 0, 80 + (int)f);

		printf("virtio_gpu_2d_render_test: frame=%u res=%u crc=0x%08x\n",
		       f, res[f], crcs[f]);
	}

	for (uint32_t i = 0; i < NFRAMES; i++)
		for (uint32_t j = i + 1; j < NFRAMES; j++)
			FAIL_IF(crcs[i] == crcs[j], 90);

	rc = uk_virtio_gpu_gl_metrics_get(dev, &m_after);
	FAIL_IF(rc != 0, 91);
	FAIL_IF(m_after.transfers_to_host < NFRAMES, 92);
	FAIL_IF(m_after.flushes < NFRAMES, 93);
	FAIL_IF(m_after.fence_waits < NFRAMES * 2u, 94);
	FAIL_IF(m_after.resources_created < NFRAMES, 95);

	uk_free(alloc, buf_vaddr);
	printf("virtio_gpu_2d_render_test: PASS frames=%u transfers=%llu flushes=%llu fences=%llu\n",
	       NFRAMES,
	       (unsigned long long)m_after.transfers_to_host,
	       (unsigned long long)m_after.flushes,
	       (unsigned long long)m_after.fence_waits);
	return 0;
}
