# SPDX-License-Identifier: BSD-3-Clause
#
# VOGUE — host-native test suite (component Makefile)
# ============================================================================
# Builds and runs the deterministic C tests in this directory against the fake
# VirtIO-GPU backend modules under fake/. No QEMU, Unikraft, GPU, or model is
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
# Use the targets here directly only to run a single binary (e.g. `make -C
# tests venus-ring`). `make -C tests` defaults to the full `native` suite.
# ============================================================================

CC ?= cc
CFLAGS ?= -D_POSIX_C_SOURCE=200112L -std=c11 -Wall -Wextra \
	-I../libs/libukvirtio_gpu/include \
	-I../libs/libukvirtgpu_drm/include \
	-I../libs/libukvulkan_venus/include \
	-I../libs/libvulkan/include \
	-Ishim \
	-I. -Ifake

# Vulkan headers for the venus generated tree (regenerated against VK_HEADER_VERSION
# 352). Use the repo-pinned Vulkan-Headers that
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

# Shared modular fake backend source files
FAKE_SRCS := fake/fake_device.c fake/fake_resource.c fake/fake_context.c fake/fake_blob.c

# Generated-encoder includes: bare "vn_cs.h"/"vn_ring.h" live in include/uk;
# the driver headers live in generated/; vulkan.h comes from VK_INC. The
# generated code is upstream-shaped, so silence its stylistic warnings.
GEN_CFLAGS := -I../libs/libukvulkan_venus/include/uk -I../libs/libukvulkan_venus/generated \
	-I$(VK_INC) -Wno-unused-parameter -Wno-missing-field-initializers \
	-Wno-uninitialized-const-pointer

VENUS := ../libs/libukvulkan_venus

# define_test: $(call define_test,binary,run-target,prereqs,extra-cflags)
# Emits the per-binary build rule plus a build+run convenience target.
define define_test
$(BUILD)/$(1): $(3) | $(BUILD)
	$$(CC) $$(CFLAGS) $(4) $$^ -o $$@
.PHONY: $(2)
$(2): $(BUILD)/$(1)
	./$(BUILD)/$(1)
endef

$(eval $(call define_test,core_test,core-test,\
	libukvirtio_gpu/core_test.c $(FAKE_SRCS),))
$(eval $(call define_test,drm_compat_test,drm-compat,\
	libukvirtgpu_drm/drm_compat_test.c ../libs/libukvirtgpu_drm/drm_virtgpu.c \
	../libs/libukvirtgpu_drm/drm_virtgpu_fdio.c $(FAKE_SRCS),))
$(eval $(call define_test,virgl_encoder_test,virgl-encoder,\
	libukvirtio_gpu/virgl_encoder_test.c ../libs/libukvirtio_gpu/virgl_encoder.c \
	$(FAKE_SRCS),-I$(VENUS)/include))
$(eval $(call define_test,venus_encoder_test,venus-encoder,\
	libukvulkan_venus/venus_encoder_test.c $(VENUS)/venus_cs.c \
	$(VENUS)/venus_compute.c,$(GEN_CFLAGS)))
$(eval $(call define_test,venus_ring_test,venus-ring,\
	libukvulkan_venus/venus_ring_test.c $(VENUS)/venus_cs.c $(VENUS)/venus_init.c \
	$(VENUS)/venus_compute.c $(FAKE_SRCS),$(GEN_CFLAGS)))
# vk.ggml-dispatch: static Vulkan dispatch layer test. Links uk_vulkan_dispatch.c
# against the fake VirtIO-GPU backend and the native Venus driver (no DRM/ICD).
$(eval $(call define_test,dispatch_test,dispatch,\
	libvulkan/dispatch_test.c ../libs/libvulkan/uk_vulkan_dispatch.c \
	$(VENUS)/venus_driver.c $(VENUS)/venus_init.c $(VENUS)/venus_cs.c \
	$(VENUS)/venus_compute.c $(VENUS)/vn_ring_shim.c $(FAKE_SRCS),$(GEN_CFLAGS) -Wno-format))

NATIVE_BINS := \
	$(BUILD)/core_test \
	$(BUILD)/drm_compat_test \
	$(BUILD)/venus_encoder_test \
	$(BUILD)/venus_ring_test \
	$(BUILD)/virgl_encoder_test \
	$(BUILD)/dispatch_test

.PHONY: native test-core test-compat test-venus test-dispatch proto-abi clean

# run_bins: compile the listed binaries in parallel, then run them serially for
# deterministic output ordering.
run_bins = +@$(MAKE) -j$(JOBS) $(1); for b in $(1); do ./$$b || exit $$?; done

# Full deterministic suite — primary CI gate.
# llama.cpp coverage lives in the upstream single-app env matrix and the
# ggml-vulkan static dispatch test; synthetic GGUF/ggml/llama substrate tests
# were removed to avoid duplicating upstream llama.cpp internals.
native:
	$(call run_bins,$(NATIVE_BINS))

# Group 1: deterministic core substrate checks.
test-core:
	$(call run_bins,$(BUILD)/core_test $(BUILD)/virgl_encoder_test)

# Group 2: compatibility / translator checks.
test-compat: drm-compat

# Group 3: Venus protocol / ring checks.
test-venus:
	$(call run_bins,$(BUILD)/venus_encoder_test $(BUILD)/venus_ring_test)

# Group 4: static Vulkan dispatch surface.
test-dispatch: dispatch

proto-abi: | $(BUILD)
	@if [ ! -f "$(PROTO_HDR)" ]; then \
		echo "proto_abi_test BLOCKED missing $(PROTO_HDR)"; \
		echo "dependency: implement planned protocol ABI header before enabling real-driver codec tests"; \
	else \
		$(MAKE) $(BUILD)/proto_abi_test && ./$(BUILD)/proto_abi_test; \
	fi

$(BUILD)/proto_abi_test: libukvirtio_gpu/proto_abi_test.c $(PROTO_HDR) | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
