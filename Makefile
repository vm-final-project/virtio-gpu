# SPDX-License-Identifier: BSD-3-Clause

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := help

KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
ARCH ?= x86_64
ifeq ($(filter $(ARCH),x86_64 arm64),)
$(error ARCH must be one of: x86_64 arm64)
endif
KRAFT_TARGET ?= qemu/$(ARCH)
EXTERNAL_DEPS_DIR ?= $(CURDIR)/.deps/src
QEMU ?= $(if $(filter $(ARCH),arm64),qemu-system-aarch64,qemu-system-x86_64)
MODEL ?= $(CURDIR)/models/model.gguf
RUN_TIMEOUT ?= 120

LLAMA_ROOT             ?= $(realpath $(EXTERNAL_DEPS_DIR)/llama.cpp)
VENUS_PROTOCOL_ROOT    ?= $(realpath $(EXTERNAL_DEPS_DIR)/venus-protocol)
VULKAN_HEADERS_INCLUDE ?= $(realpath $(EXTERNAL_DEPS_DIR)/Vulkan-Headers/include)
SPIRV_HEADERS_INCLUDE  ?= $(realpath $(EXTERNAL_DEPS_DIR)/SPIRV-Headers/include)
VK_LIB                 ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
LLAMA_BUILD_JOBS       ?= 8
HOST_CXX_INCLUDE       ?=
HOST_GCC_LIB           ?=
VOGUE_MARCH            ?= $(if $(filter $(ARCH),arm64),armv8-a,native)

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE
export SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB
export ARCH KRAFT_TARGET EXTERNAL_DEPS_DIR VOGUE_MARCH

include mk/tests.mk
include mk/llama.mk
include mk/evidence.mk

.PHONY: help verify deps deps-status deps-refresh clean

help:
	@printf '%s\n' \
	  'VOGUE targets' \
	  '' \
	  'Config:     ARCH={x86_64|arm64} KRAFT_TARGET=$(KRAFT_TARGET)' \
	  'Tests:      test-fast test-native venus-check vulkan-check verify' \
	  'Build/run:  llama-{cpu,vk}{,-server}-{build,run}' \
	  'Deps:       deps deps-status deps-refresh' \
	  'Baseline:   linux-guest-vk-baseline' \
	  'Cleanup:    clean'

verify: test-fast venus-check vulkan-check \
	llama-cpu-run llama-cpu-server-run llama-vk-run llama-vk-server-run \
	linux-guest-vk-baseline

deps:
	python3 scripts/deps.py fetch

deps-status:
	python3 scripts/deps.py status

deps-refresh:
	python3 scripts/deps.py fetch --refresh

clean:
	$(MAKE) -C tests clean
	find results -type f \( -name '*.log' -o -name '*.ppm' -o -name '*.tmp' \) -delete
	rm -rf results/kmscube_vgpu_gl/run/.qmp
