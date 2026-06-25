# SPDX-License-Identifier: BSD-3-Clause
#
# VOGUE — host-native test suite (component Makefile)
# ============================================================================
# Builds and runs host-native tests that do not instantiate a fake VirtIO-GPU
# device. No QEMU, Unikraft, GPU, or model is required. Each test compiles
# project library sources directly and links one self-checking binary under
# build/.
#
# The repository root Makefile wraps the everyday groups so reviewers can stay
# at the top level:
#     make native-tests   ->  make -C tests native
#     make test-core      ->  make -C tests test-core
#     make test-venus     ->  make -C tests test-venus
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
	-I.

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
PROTO_HDR := ../libs/libukvirtio_gpu/protocol/virtio_gpu_proto.h

# Default parallelism: one job per detected core; reviewers can override via
# `make -C tests native -j1` if needed.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += --no-print-directory

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

$(eval $(call define_test,venus_encoder,venus-encoder,\
	$(VENUS)/tests/encoder.c $(VENUS)/vn_cs.c \
	$(VENUS)/vn_device_memory.c $(VENUS)/vn_buffer.c $(VENUS)/vn_pipeline.c \
	$(VENUS)/vn_descriptor_set.c $(VENUS)/vn_command_buffer.c $(VENUS)/vn_queue.c,$(GEN_CFLAGS)))
$(eval $(call define_test,venus_capset,venus-capset,\
	$(VENUS)/tests/capset.c $(VENUS)/vn_renderer_util.c,$(GEN_CFLAGS)))

NATIVE_BINS := \
	$(BUILD)/venus_encoder \
	$(BUILD)/venus_capset

.PHONY: native test-venus \
	venus-encoder-core venus-capset-core \
	proto-abi clean

# run_bins: compile the listed binaries in parallel, then run them serially for
# deterministic output ordering.
run_bins = +@$(MAKE) -j$(JOBS) $(1); for b in $(1); do ./$$b || exit $$?; done

# Full deterministic suite — primary CI gate.
# llama.cpp coverage lives in the upstream single-app env matrix and the
# ggml-vulkan static dispatch test; synthetic GGUF/ggml/llama substrate tests
# were removed to avoid duplicating upstream llama.cpp internals.
native:
	$(call run_bins,$(NATIVE_BINS))

# Group 2: Venus protocol checks.
test-venus:
	$(call run_bins,$(BUILD)/venus_encoder $(BUILD)/venus_capset)

# Stable reviewer-facing aliases documented in AGENTS.md and implementation
# plans. Keep the short local target names above as the implementation detail.
venus-encoder-core: venus-encoder
venus-capset-core: venus-capset

proto-abi: | $(BUILD)
	@if [ ! -f "$(PROTO_HDR)" ]; then \
		echo "proto_abi BLOCKED missing $(PROTO_HDR)"; \
		echo "dependency: implement planned protocol ABI header before enabling real-driver codec tests"; \
	else \
		$(MAKE) $(BUILD)/proto_abi && ./$(BUILD)/proto_abi; \
	fi

$(BUILD)/proto_abi: ../libs/libukvirtio_gpu/tests/proto_abi.c $(PROTO_HDR) | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
