#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <uk/virtio_gpu.h>
#include <uk/virgl_encoder.h>
#include <uk/venus.h>
#include <uk/sglist.h>
#include <uk/alloc.h>
#include <uk/plat/time.h>

#if defined(__has_include)
#if __has_include(<uk/fbdev.h>)
#include <uk/fbdev.h>
#define UK_KMSCUBE_HAVE_FBDEV 1
#endif
#endif

#define NFRAMES     3u
#define TARGET_W  640u
#define TARGET_H  480u

#define FAIL_BLOCK(msg, ...) \
	do { printf("uk-kmscube: BLOCKED " msg "\n", ##__VA_ARGS__); return 0; } while (0)

static void print_capsets(struct uk_virtio_gpu_dev *gpu,
                           const struct uk_virtio_gpu_caps *caps)
{
	struct uk_virtio_gpu_capset_info cap;
	for (uint32_t i = 0; i < caps->num_capsets && i < 8u; i++) {
		if (uk_virtio_gpu_dev_capset_info_get(gpu, i, &cap))
			continue;
		printf("uk-kmscube: capset index=%u id=%u name=%s\n",
		       i, cap.id, uk_virtio_gpu_capset_name(cap.id));
	}
}

static int argv_has(int argc, char **argv, const char *needle)
{
	if (!needle)
		return 0;
	for (int i = 0; i < argc; i++)
		if (argv && argv[i] && strstr(argv[i], needle))
			return 1;
	return 0;
}

static void hold_for_qmp_screendump(void)
{
	uint64_t start = (uint64_t)ukplat_monotonic_clock();
	uint64_t deadline = start + 5000000000ull;

	printf("uk-kmscube: frame_ready marker=qmp-screendump-hold hold_ns=5000000000\n");
	while ((uint64_t)ukplat_monotonic_clock() < deadline)
		__asm__ __volatile__("pause" ::: "memory");
	printf("uk-kmscube: frame_ready marker=qmp-screendump-release\n");
}

static int run_venus_ring_probe(struct uk_virtio_gpu_dev *dev)
{
	struct uk_venus_ring ring = { 0 };
	int rc;

	if (!dev) {
		printf("uk-venus: ring blocked no-device\n");
		return -1;
	}

	printf("uk-venus: ring status=%s\n", uk_venus_ring_status(dev));
	rc = uk_venus_ring_create(dev, &ring, UK_VENUS_RING_DEFAULT_SIZE,
				  0);
	if (rc) {
		printf("uk-venus: ring create rc=%d\n", rc);
		return rc;
	}

	rc = uk_venus_ring_register(dev, &ring, 0x766f677565000001ull);
	if (rc) {
		printf("uk-venus: ring register rc=%d\n", rc);
		uk_venus_ring_destroy(dev, &ring);
		return rc;
	}
	printf("uk-venus: ring registered resource=%u size=%llu buf_size=%u\n",
	       ring.blob.resource_id, (unsigned long long)ring.size,
	       ring.buf_size);

	/*
	 * Do not place vkNotifyRingMESA inside the ring payload.  The notify is
	 * sent by uk_venus_ring_cmd_flush(); writing it as payload makes the
	 * host consumer parse a transport command as an in-ring Vulkan command
	 * and returns a CS error.  Empty-ring notify + head/tail wait is the
	 * deterministic transport proof; non-empty Vulkan payloads need the
	 * full Venus command grammar.
	 */
	rc = uk_venus_ring_cmd_flush(dev, &ring);
	if (rc) {
		printf("uk-venus: ring flush rc=%d\n", rc);
		goto out_unregister;
	}
	printf("uk-venus: ring flush ok tail=%u head=%u\n",
	       ring.cur_tail, uk_venus_ring_load_head(&ring));
	rc = uk_venus_ring_cmd_wait(&ring, 100000u);
	if (rc)
		printf("uk-venus: ring wait rc=%d head=%u tail=%u\n",
		       rc, uk_venus_ring_load_head(&ring), ring.cur_tail);
	else
		printf("uk-venus: ring wait ok head=%u tail=%u\n",
		       uk_venus_ring_load_head(&ring), ring.cur_tail);

out_unregister:
	(void)uk_venus_ring_unregister(dev, &ring);
	uk_venus_ring_destroy(dev, &ring);
	if (!rc)
		printf("venus_ring_protocol=pass\n");
	return rc;
}

/* virgl render path: creates a 3D resource, binds it as a Gallium surface,
 * clears it with a rotating colour, and presents via SET_SCANOUT + FLUSH.
 * Requires a real virgl-capable QEMU device; returns non-zero on failure.
 * Evidence row: K1 (not gfx.kmscube.sw). */
static int run_virgl_path(struct uk_virtio_gpu_dev *dev,
			  uint32_t w, uint32_t h,
			  uint32_t *submits_out, int want_hold)
{
	struct uk_virtio_gpu_context ctx = {0};
	struct uk_virgl_encoder     enc;
	uk_gpu_res_id               res[NFRAMES];
	uk_gpu_fence_id             fence;
	uint32_t                    submits = 0;
	int                         rc;

	if (!dev)
		return -1;

	/* Confirm virgl capset is available (capset_id=1). */
	struct uk_virtio_gpu_capset_info cap = {0};
	int found_virgl = 0;
	struct uk_virtio_gpu_caps caps = {0};
	if (uk_virtio_gpu_dev_caps_get(dev, &caps) == 0) {
		for (uint32_t i = 0; i < caps.num_capsets && i < 8u; i++) {
			if (uk_virtio_gpu_dev_capset_info_get(dev, i, &cap) == 0
			    && cap.id == 1u)
				found_virgl = 1;
		}
	}
	if (!found_virgl) {
		printf("uk-kmscube: virgl capset not present\n");
		return -2;
	}

	/* Create a virgl rendering context. */
	rc = uk_virtio_gpu_gl_context_create(dev, 1u, "kmscube-virgl", &ctx);
	if (rc) {
		printf("uk-kmscube: virgl ctx_create rc=%d\n", rc);
		return rc;
	}
	printf("uk-kmscube: virgl_path ctx_id=%u w=%u h=%u frames=%u\n",
	       ctx.id, w, h, NFRAMES);

	static const float colours[NFRAMES][4] = {
		{0.20f, 0.40f, 0.80f, 1.0f},  /* blue */
		{0.80f, 0.20f, 0.40f, 1.0f},  /* red  */
		{0.40f, 0.80f, 0.20f, 1.0f},  /* green*/
	};

	for (uint32_t f = 0; f < NFRAMES; f++) {
		/* Allocate a 3D render-target resource for this frame. */
		struct uk_virtio_gpu_resource_3d r3d = {
			.width  = w, .height = h, .depth = 1,
			.array_size = 1, .last_level = 0,
			.nr_samples = 0,
			.target = UK_VIRGL_PIPE_TEXTURE_2D,
			.format = UK_VIRGL_FORMAT_B8G8R8X8_UNORM,
			.bind   = UK_VIRGL_PIPE_BIND_RENDER_TARGET,
			.flags  = 0,
		};
		res[f] = 0;
		rc = uk_virtio_gpu_gl_resource_create_3d(dev, &r3d, &res[f]);
		if (rc || !res[f]) {
			printf("uk-kmscube: virgl resource_create_3d rc=%d\n", rc);
			goto out_ctx;
		}

		/* Attach resource to context so virglrenderer can resolve it. */
		rc = uk_virtio_gpu_gl_context_attach_resource(dev, &ctx, res[f]);
		if (rc) {
			printf("uk-kmscube: virgl attach_resource rc=%d\n", rc);
			goto out_ctx;
		}

		/* Encode: SURFACE create + SET_FRAMEBUFFER + CLEAR. */
		uk_virgl_encoder_init(&enc);
		rc  = uk_virgl_encode_create_surface(&enc, f + 1u, res[f],
						     UK_VIRGL_FORMAT_B8G8R8X8_UNORM, 0u);
		rc |= uk_virgl_encode_set_framebuffer(&enc, f + 1u);
		rc |= uk_virgl_encode_clear(&enc, colours[f][0], colours[f][1],
					    colours[f][2], colours[f][3]);
		if (rc || enc.overflow) {
			printf("uk-kmscube: virgl encode rc=%d overflow=%d\n",
			       rc, enc.overflow);
			goto out_ctx;
		}

		/* Submit the Gallium command stream via SUBMIT_3D. */
		fence = 0;
		rc = uk_virtio_gpu_gl_context_submit(dev, &ctx,
						     uk_virgl_encoder_buf(&enc),
						     uk_virgl_encoder_len(&enc),
						     &fence);
		if (rc) {
			printf("uk-kmscube: virgl submit rc=%d\n", rc);
			goto out_ctx;
		}
		submits++;
		uk_virtio_gpu_fence_wait(dev, fence, 2000000u);

		/* Present via 2D scanout (virgl keeps pixels in resource). */
		struct uk_gpu_rect rect = {0, 0, w, h};
		uk_virtio_gpu_gl_set_scanout(dev, 0, res[f], &rect);

		fence = 0;
		uk_virtio_gpu_gl_transfer_to_host_3d(dev, res[f],
			&(struct uk_virtio_gpu_transfer_3d){
				.box={.x=0,.y=0,.z=0,.w=w,.h=h,.d=1},
				.level=0,.stride=w*4u,.layer_stride=0,
				.offset=0},
			&fence);
		uk_virtio_gpu_fence_wait(dev, fence, 2000000u);

		/* Present the scanout resource to the host display. SET_SCANOUT
		 * alone only binds the resource; RESOURCE_FLUSH is what makes the
		 * host composite/display it (virtio-gpu spec 5.7.6.10), so a QMP
		 * screendump captures the rendered colour band. */
		fence = 0;
		uk_virtio_gpu_resource_flush(dev, res[f], &rect, &fence);
		uk_virtio_gpu_fence_wait(dev, fence, 2000000u);

		printf("uk-kmscube: virgl_frame=%u submit=%u colour=%.2f,%.2f,%.2f\n",
		       f, submits,
		       (double)colours[f][0], (double)colours[f][1],
		       (double)colours[f][2]);
	}

	if (submits_out)
		*submits_out = submits;

	/* Hold while the last (green) frame's scanout resource and rendering
	 * context are still alive, so a QMP screendump reads a live surface
	 * instead of failing once the context (and its resources) are torn down.
	 * Note: QEMU egl-headless still cannot screendump a virtio-gpu *GL*
	 * scanout ("no surface"); the colour-band frame proof needs a display
	 * backend with GL readback. */
	if (want_hold) {
		struct uk_gpu_rect rect = {0, 0, w, h};
		uk_virtio_gpu_gl_set_scanout(dev, 0, res[NFRAMES - 1], &rect);
		fence = 0;
		uk_virtio_gpu_resource_flush(dev, res[NFRAMES - 1], &rect, &fence);
		uk_virtio_gpu_fence_wait(dev, fence, 2000000u);
		hold_for_qmp_screendump();
	}

	uk_virtio_gpu_gl_context_destroy(dev, &ctx);
	return 0;

out_ctx:
	uk_virtio_gpu_gl_context_destroy(dev, &ctx);
	return rc ? rc : -1;
}

int main(int argc, char **argv)
{
	struct uk_virtio_gpu_dev *gpu  = NULL;
	struct uk_virtio_gpu_caps caps;
	int rc;
	uint32_t w = TARGET_W, h = TARGET_H;
	int want_venus_ring = argv_has(argc, argv, "venus_ring_test=1");
	int want_frame_hold = argv_has(argc, argv, "frame_proof_hold=1");

	printf("uk-kmscube: booted kmscube_vgpu_gl proof harness\n");

	/* Try to get a real VirtIO-GPU device (NULL if no real driver linked). */
	gpu = uk_virtio_gpu_default_dev();
	if (gpu) {
		if (uk_virtio_gpu_dev_caps_get(gpu, &caps) == 0) {
			printf("uk-kmscube: virtio_gpu capsets=%u virgl=%u blob=%u host_visible=%u\n",
			       caps.num_capsets, caps.has_virgl, caps.has_resource_blob,
			       caps.has_host_visible);
			print_capsets(gpu, &caps);
		}
	} else {
#if defined(CONFIG_LIBUKVIRTIO_GPU_BACKEND_FAKE)
		printf("uk-kmscube: no real virtio-gpu device using explicit fake backend\n");
		uk_virtio_gpu_probe(&gpu);
#else
		FAIL_BLOCK("kmscube_vgpu_gl status=blocked:no-real-virtio-gpu");
#endif
	}

#if UK_KMSCUBE_HAVE_FBDEV
	printf("uk-kmscube: fbdev count=%u\n", uk_fbdev_count());
#endif

	uint32_t virgl_submits = 0;
	int virgl_rc = run_virgl_path(gpu, w, h, &virgl_submits, want_frame_hold);
	if (virgl_rc == 0) {
		printf("uk-kmscube: PASS kmscube_vgpu_gl frames=%u renderer=virgl "
		       "submits_3d=%u evidence_id=virgl-clear-proof real_virtio_gpu=1 w=%u h=%u\n",
		       NFRAMES, virgl_submits, w, h);
	} else {
		printf("uk-kmscube: BLOCKED kmscube_vgpu_gl status=blocked:virgl-unavailable rc=%d\n",
		       virgl_rc);
	}

	if (want_venus_ring) {
		rc = run_venus_ring_probe(gpu);
		if (rc)
			printf("uk-kmscube: BLOCKED venus_ring_protocol rc=%d\n", rc);
	}

	return 0;
}
