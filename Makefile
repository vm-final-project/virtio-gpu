# SPDX-License-Identifier: BSD-3-Clause

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := help

KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
QEMU ?= qemu-system-x86_64

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
	  'Tests:      test-fast test-native test-qemu test-gpu verify' \
	  'Evidence:   venus-check vulkan-check eval-check current-stage-check' \
	  'Servers:    llm-server-vk-check llm-server-vk-throughput-check' \
	  'Build/run:  llama-{cpu,vk}{,-server}-{build,run}' \
	  'Baseline:   linux-guest-vk-baseline' \
	  'Cleanup:    clean'

verify: test-fast test-qemu test-gpu eval-check current-stage-check \
	llm-server-vk-throughput-check

clean:
	$(MAKE) -C tests clean
