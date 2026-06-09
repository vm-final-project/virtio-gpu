# SPDX-License-Identifier: BSD-3-Clause

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := help

KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
QEMU ?= qemu-system-x86_64
MODEL ?= $(CURDIR)/models/model.gguf
RUN_TIMEOUT ?= 120

LLAMA_ROOT             ?= $(realpath $(CURDIR)/../llama.cpp)
VENUS_PROTOCOL_ROOT    ?= $(realpath $(CURDIR)/../venus-protocol)
VULKAN_HEADERS_INCLUDE ?= $(realpath $(CURDIR)/../Vulkan-Headers/include)
SPIRV_HEADERS_INCLUDE  ?= $(realpath $(CURDIR)/../SPIRV-Headers/include)
VK_LIB                 ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
LLAMA_BUILD_JOBS       ?= 8
HOST_CXX_INCLUDE       ?=
HOST_GCC_LIB           ?=

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE
export SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB

include mk/tests.mk
include mk/llama.mk
include mk/evidence.mk

.PHONY: help verify clean

help:
	@printf '%s\n' \
	  'VOGUE targets' \
	  '' \
	  'Tests:      test-fast test-native venus-check vulkan-check verify' \
	  'Build/run:  llama-{cpu,vk}{,-server}-{build,run}' \
	  'Baseline:   linux-guest-vk-baseline' \
	  'Cleanup:    clean'

verify: test-fast venus-check vulkan-check \
	llama-cpu-run llama-cpu-server-run llama-vk-run llama-vk-server-run \
	linux-guest-vk-baseline

clean:
	$(MAKE) -C tests clean
	find results -type f \( -name '*.log' -o -name '*.ppm' -o -name '*.tmp' \) -delete
	rm -rf results/kmscube_vgpu_gl/run/.qmp
