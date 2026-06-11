# SPDX-License-Identifier: BSD-3-Clause
#
# VOGUE — host-native test suite (component Makefile)
# ============================================================================
# Builds and runs the deterministic C tests in this directory against the fake
# VirtIO-GPU backend (virtio_gpu_fake.c). No QEMU, Unikraft, GPU, or model is
# required. Each test compiles project library sources directly and links one
# self-checking binary under build/.
#
# The repository root Makefile wraps the everyday groups so reviewers can stay
# at the top level:
#     make native-tests   ->  make -C tests native
#     make test-core      ->  make -C tests test-core
#     make test-compat    ->  make -C tests test-compat
#     make test-venus     ->  make -C tests test-venus
#     make test-dispatch  ->  make -C tests test-dispatch
#     make proto-abi      ->  make -C tests proto-abi
#     make vulkan-tests   ->  make -C tests vulkan
# Use the targets here directly only to run a single binary (e.g. `make -C
# tests venus-ring-core`). `make -C tests` defaults to the full `native` suite.
# ============================================================================

CC ?= cc
CFLAGS ?= -D_POSIX_C_SOURCE=200112L -std=c11 -Wall -Wextra \
	-I../libs/libukvirtio_gpu/include \
	-I../libs/libukvirtgpu_drm/include \
	-I../libs/libukvulkan_venus/include \
	-I../libs/libvulkan/include \
	-Ishim

# Vulkan headers for the venus generated tree (regenerated against VK_HEADER_VERSION
# 352) and the host compute baseline. Use the repo-pinned Vulkan-Headers that
# `make deps` fetches and the image build compiles against (VULKAN_HEADERS_INCLUDE
# exported by the root Makefile); fall back to the local `.deps/src` checkout when
# running `make -C tests` directly. The old sibling `../../venus-protocol/include`
# default left VK_INC empty here, so the build picked up the stale system
# `vulkan.h` (v204) and failed on newer Vulkan types.
VK_INC ?= $(or $(VULKAN_HEADERS_INCLUDE),$(realpath ../.deps/src/Vulkan-Headers/include))
VK_LIB ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
BUILD := build
PROTO_HDR := ../libs/libukvirtio_gpu/virtio_gpu_proto.h

# Default parallelism: one job per detected core; reviewers can override via
# `make -C tests native -j1` if needed.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += --no-print-directory

.PHONY: native test-core test-compat test-venus test-dispatch proto-abi vulkan \
	virtio-gpu-core virtgpu-drm-compat venus-encoder-core venus-ring-core \
	virgl-encoder-core vulkan-dispatch-core clean

NATIVE_BINS := \
	$(BUILD)/virtio_gpu_core_test \
	$(BUILD)/virtgpu_drm_compat_test \
	$(BUILD)/venus_encoder_core_test \
	$(BUILD)/venus_ring_core_test \
	$(BUILD)/virgl_encoder_core_test \
	$(BUILD)/vulkan_dispatch_core_test

# Full deterministic suite — primary CI gate. Two phases: parallel compile
# (one job per CPU core) then serial run for deterministic output ordering.
# llama.cpp coverage lives in the upstream single-app env matrix and the
# ggml-vulkan static dispatch test; synthetic GGUF/ggml/llama substrate tests
# were removed to avoid duplicating upstream llama.cpp internals.
native:
	+@$(MAKE) -j$(JOBS) $(NATIVE_BINS)
	@for b in $(NATIVE_BINS); do ./$$b || exit $$?; done

# Group 1: deterministic core substrate checks.
TEST_CORE_BINS := \
	$(BUILD)/virtio_gpu_core_test \
	$(BUILD)/virgl_encoder_core_test
test-core:
	+@$(MAKE) -j$(JOBS) $(TEST_CORE_BINS)
	@for b in $(TEST_CORE_BINS); do ./$$b || exit $$?; done

# Group 2: compatibility / translator checks.
test-compat: $(BUILD)/virtgpu_drm_compat_test
	./$(BUILD)/virtgpu_drm_compat_test

# Group 3: Venus protocol / ring checks.
TEST_VENUS_BINS := \
	$(BUILD)/venus_encoder_core_test \
	$(BUILD)/venus_ring_core_test
test-venus:
	+@$(MAKE) -j$(JOBS) $(TEST_VENUS_BINS)
	@for b in $(TEST_VENUS_BINS); do ./$$b || exit $$?; done

# Group 4: static Vulkan dispatch surface.
test-dispatch: $(BUILD)/vulkan_dispatch_core_test
	./$(BUILD)/vulkan_dispatch_core_test

virtio-gpu-core: $(BUILD)/virtio_gpu_core_test
	./$(BUILD)/virtio_gpu_core_test

virtgpu-drm-compat: $(BUILD)/virtgpu_drm_compat_test
	./$(BUILD)/virtgpu_drm_compat_test

virgl-encoder-core: $(BUILD)/virgl_encoder_core_test
	./$(BUILD)/virgl_encoder_core_test

venus-encoder-core: $(BUILD)/venus_encoder_core_test
	./$(BUILD)/venus_encoder_core_test

venus-ring-core: $(BUILD)/venus_ring_core_test
	./$(BUILD)/venus_ring_core_test

vulkan-dispatch-core: $(BUILD)/vulkan_dispatch_core_test
	./$(BUILD)/vulkan_dispatch_core_test

vulkan: | $(BUILD)
	@if [ -f "$(VK_LIB)" ] && [ -d "$(VK_INC)" ]; then \
		$(MAKE) $(BUILD)/vulkan_compute_test && ./$(BUILD)/vulkan_compute_test; \
	else \
		echo "vulkan_compute_test BLOCKED: VK_LIB=$(VK_LIB) VK_INC=$(VK_INC) not found"; \
	fi

proto-abi: | $(BUILD)
	@if [ ! -f "$(PROTO_HDR)" ]; then \
		echo "virtio_gpu_proto_abi_test BLOCKED missing $(PROTO_HDR)"; \
		echo "dependency: implement planned protocol ABI header before enabling real-driver codec tests"; \
	else \
		$(MAKE) $(BUILD)/virtio_gpu_proto_abi_test && ./$(BUILD)/virtio_gpu_proto_abi_test; \
	fi

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

$(BUILD)/virtio_gpu_core_test: virtio_gpu_core_test.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/virtio_gpu_proto_abi_test: virtio_gpu_proto_abi_test.c $(PROTO_HDR) | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/virtgpu_drm_compat_test: virtgpu_drm_compat_test.c \
	../libs/libukvirtgpu_drm/drm_virtgpu.c \
	../libs/libukvirtgpu_drm/drm_virtgpu_fdio.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/venus_encoder_core_test: venus_encoder_core_test.c \
	../libs/libukvulkan_venus/venus_cs.c \
	../libs/libukvulkan_venus/venus_compute.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

$(BUILD)/venus_ring_core_test: venus_ring_core_test.c \
	../libs/libukvulkan_venus/venus_cs.c \
	../libs/libukvulkan_venus/venus_init.c \
	../libs/libukvulkan_venus/venus_compute.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

# Generated-encoder includes: bare "vn_cs.h"/"vn_ring.h" live in include/uk;
# the driver headers live in generated/; vulkan.h comes from VK_INC. The
# generated code is upstream-shaped, so silence its stylistic warnings.
GEN_CFLAGS := -I../libs/libukvulkan_venus/include/uk -I../libs/libukvulkan_venus/generated \
	-I$(VK_INC) -Wno-unused-parameter -Wno-missing-field-initializers \
	-Wno-uninitialized-const-pointer

$(BUILD)/virgl_encoder_core_test: virgl_encoder_core_test.c ../libs/libukvirtio_gpu/virgl_encoder.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) -I../libs/libukvulkan_venus/include $^ -o $@

$(BUILD)/vulkan_compute_test: vulkan_compute_test.c | $(BUILD)
	$(CC) -std=c11 -D_POSIX_C_SOURCE=199309L -Wall -Wno-missing-field-initializers -I$(VK_INC) $< $(VK_LIB) -lm -o $@

# vk.ggml-dispatch: static Vulkan dispatch layer test. Links uk_vulkan_dispatch.c
# against the fake VirtIO-GPU backend and the native Venus driver (no DRM/ICD).
$(BUILD)/vulkan_dispatch_core_test: vulkan_dispatch_core_test.c \
	../libs/libvulkan/uk_vulkan_dispatch.c \
	../libs/libukvulkan_venus/venus_driver.c \
	../libs/libukvulkan_venus/venus_init.c \
	../libs/libukvulkan_venus/venus_cs.c \
	../libs/libukvulkan_venus/venus_compute.c \
	../libs/libukvulkan_venus/vn_ring_shim.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) -Wno-format $^ -o $@
