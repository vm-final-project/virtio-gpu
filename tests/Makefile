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
#     make test-venus     ->  make -C tests test-venus
#     make test-dispatch  ->  make -C tests test-dispatch
#     make proto-abi      ->  make -C tests proto-abi
#     make vulkan-tests   ->  make -C tests vulkan
# Use the targets here directly only to run a single binary (e.g. `make -C
# tests venus-cs`). `make -C tests` defaults to the full `native` suite.
# ============================================================================

CC ?= cc
CFLAGS ?= -D_POSIX_C_SOURCE=200112L -std=c11 -Wall -Wextra \
	-I../libs/libukvirtio_gpu/include \
	-I../libs/libukdrm_compat/include \
	-I../libs/libukgbm_compat/include \
	-I../libs/libukvirtgpu_drm/include \
	-I../libs/libukvk_icd/include \
	-I../libs/libukvenus/include \
	-I../libs/libukggml_vk/include \
	-Ishim

# Default to the sibling venus-protocol checkout; system loader on the host.
VK_INC ?= $(realpath ../../venus-protocol/include)
VK_LIB ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
BUILD := build
PROTO_HDR := ../libs/libukvirtio_gpu/virtio_gpu_proto.h

# Default parallelism: one job per detected core; reviewers can override via
# `make -C tests native -j1` if needed.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += --no-print-directory

.PHONY: native test-core test-venus test-dispatch test-n3 proto-abi vulkan g5 venus-cs venus-compute venus-gen-compile venus-parity virgl-enc n3-dispatch clean

NATIVE_BINS := \
	$(BUILD)/dma_buf_test \
	$(BUILD)/virtio_gpu_full_api_test \
	$(BUILD)/virtio_gpu_2d_render_test \
	$(BUILD)/kmscube_compat_test \
	$(BUILD)/virtgpu_drm_ioctl_test \
	$(BUILD)/vk_icd_bootstrap_test \
	$(BUILD)/venus_cs_test \
	$(BUILD)/venus_compute_test \
	$(BUILD)/virgl_encoder_test \
	$(BUILD)/venus_generated_compile_test \
	$(BUILD)/venus_parity_test \
	$(BUILD)/ggml_vk_dispatch_test

# Full deterministic suite — primary CI gate. Two phases: parallel compile
# (one job per CPU core) then serial run for deterministic output ordering.
# llama.cpp coverage lives in the upstream single-app env matrix and the
# ggml-vulkan static dispatch test; synthetic GGUF/ggml/llama substrate tests
# were removed to avoid duplicating upstream llama.cpp internals.
native:
	+@$(MAKE) -j$(JOBS) $(NATIVE_BINS)
	@for b in $(NATIVE_BINS); do ./$$b || exit $$?; done

# Group 1: VirtIO-GPU core, DMA, shims (proto.real-driver, gfx.kmscube.sw)
TEST_CORE_BINS := \
	$(BUILD)/dma_buf_test \
	$(BUILD)/virtio_gpu_full_api_test \
	$(BUILD)/virtio_gpu_2d_render_test \
	$(BUILD)/kmscube_compat_test
test-core:
	+@$(MAKE) -j$(JOBS) $(TEST_CORE_BINS)
	@for b in $(TEST_CORE_BINS); do ./$$b || exit $$?; done

# Group 2: VirtIO-GPU 3D / Venus / virgl (vk.drm-shim, vk.icd, proto.venus-enc, proto.venus-ring)
TEST_VENUS_BINS := \
	$(BUILD)/virtgpu_drm_ioctl_test \
	$(BUILD)/vk_icd_bootstrap_test \
	$(BUILD)/venus_cs_test \
	$(BUILD)/venus_compute_test \
	$(BUILD)/virgl_encoder_test \
	$(BUILD)/venus_generated_compile_test \
	$(BUILD)/venus_parity_test
test-venus:
	+@$(MAKE) -j$(JOBS) $(TEST_VENUS_BINS)
	@for b in $(TEST_VENUS_BINS); do ./$$b || exit $$?; done

# Group 3: vk.ggml-dispatch static Vulkan dispatch for upstream ggml-vulkan (vk.ggml-dispatch)
test-dispatch: $(BUILD)/ggml_vk_dispatch_test
	./$(BUILD)/ggml_vk_dispatch_test

# Legacy alias kept for existing notes/scripts.
test-n3: test-dispatch

venus-cs: $(BUILD)/venus_cs_test
	./$(BUILD)/venus_cs_test

virgl-enc: $(BUILD)/virgl_encoder_test
	./$(BUILD)/virgl_encoder_test

g5: $(BUILD)/virtgpu_drm_ioctl_test
	./$(BUILD)/virtgpu_drm_ioctl_test

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

$(BUILD)/dma_buf_test: dma_buf_test.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/virtio_gpu_full_api_test: virtio_gpu_full_api_test.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/kmscube_compat_test: kmscube_compat_test.c ../libs/libukdrm_compat/drm_compat.c ../libs/libukgbm_compat/gbm_compat.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/virtio_gpu_2d_render_test: virtio_gpu_2d_render_test.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/virtio_gpu_proto_abi_test: virtio_gpu_proto_abi_test.c $(PROTO_HDR) | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/virtgpu_drm_ioctl_test: virtgpu_drm_ioctl_test.c ../libs/libukvirtgpu_drm/drm_virtgpu.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/vk_icd_bootstrap_test: vk_icd_bootstrap_test.c ../libs/libukvk_icd/vulkan_icd.c ../libs/libukvirtgpu_drm/drm_virtgpu.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/venus_cs_test: venus_cs_test.c ../libs/libukvenus/venus_cs.c ../libs/libukvenus/venus_init.c ../libs/libukvenus/venus_compute.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

$(BUILD)/venus_compute_test: venus_compute_test.c ../libs/libukvenus/venus_cs.c ../libs/libukvenus/venus_compute.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

venus-compute: $(BUILD)/venus_compute_test
	./$(BUILD)/venus_compute_test

# Generated-encoder includes: bare "vn_cs.h"/"vn_ring.h" live in include/uk;
# the driver headers live in generated/; vulkan.h comes from VK_INC. The
# generated code is upstream-shaped, so silence its stylistic warnings.
GEN_CFLAGS := -I../libs/libukvenus/include/uk -I../libs/libukvenus/generated \
	-I$(VK_INC) -Wno-unused-parameter -Wno-missing-field-initializers

$(BUILD)/venus_generated_compile_test: venus_generated_compile_test.c \
	../libs/libukvenus/venus_cs.c ../libs/libukvenus/venus_init.c \
	../libs/libukvenus/venus_compute.c ../libs/libukvenus/vn_ring_shim.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

venus-gen-compile: $(BUILD)/venus_generated_compile_test
	./$(BUILD)/venus_generated_compile_test

$(BUILD)/venus_parity_test: venus_parity_test.c \
	../libs/libukvenus/venus_cs.c ../libs/libukvenus/venus_init.c \
	../libs/libukvenus/venus_compute.c ../libs/libukvenus/vn_ring_shim.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

venus-parity: $(BUILD)/venus_parity_test
	./$(BUILD)/venus_parity_test

$(BUILD)/virgl_encoder_test: virgl_encoder_test.c ../libs/libukvirtio_gpu/virgl_encoder.c ./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) -I../libs/libukvenus/include $^ -o $@

$(BUILD)/vulkan_compute_test: vulkan_compute_test.c | $(BUILD)
	$(CC) -std=c11 -D_POSIX_C_SOURCE=199309L -Wall -Wno-missing-field-initializers -I$(VK_INC) $< $(VK_LIB) -lm -o $@

# vk.ggml-dispatch: static Vulkan ICD dispatch layer test. Links uk_vulkan_dispatch.c against
# the fake VirtIO-GPU backend and the full Venus/ICD/vk.drm-shim stack.
$(BUILD)/ggml_vk_dispatch_test: ggml_vk_dispatch_test.c \
	../libs/libukggml_vk/uk_vulkan_dispatch.c \
	../libs/libukvk_icd/vulkan_icd.c \
	../libs/libukvirtgpu_drm/drm_virtgpu.c \
	../libs/libukvenus/venus_init.c \
	../libs/libukvenus/venus_cs.c \
	../libs/libukvenus/venus_compute.c \
	../libs/libukvenus/vn_ring_shim.c \
	./virtio_gpu_fake.c | $(BUILD)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $^ -o $@

n3-dispatch: $(BUILD)/ggml_vk_dispatch_test
	./$(BUILD)/ggml_vk_dispatch_test
