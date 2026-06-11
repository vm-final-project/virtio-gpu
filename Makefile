# SPDX-License-Identifier: BSD-3-Clause

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := help

# macOS ships GNU make 3.81, but unikraft's build requires >= 4.1. Homebrew's
# `make` formula exposes GNU make 4.x as `make` under libexec/gnubin. Prepend
# that dir to PATH so KraftKit's unikraft sub-make picks up the modern make.
# No-op on Linux or when Homebrew make is absent (wildcard yields nothing).
GNUBIN := $(firstword $(wildcard /opt/homebrew/opt/make/libexec/gnubin /usr/local/opt/make/libexec/gnubin))
ifneq ($(GNUBIN),)
export PATH := $(GNUBIN):$(PATH)
endif

KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
ARCH ?= x86_64
ifeq ($(filter $(ARCH),x86_64 arm64),)
$(error ARCH must be one of: x86_64 arm64)
endif
KRAFT_TARGET ?= qemu/$(ARCH)
EXTERNAL_DEPS_DIR ?= $(CURDIR)/.deps/src
QEMU ?= $(if $(filter $(ARCH),arm64),qemu-system-aarch64,qemu-system-x86_64)
# Default model: prefer models/model.gguf, otherwise fall back to the sole
# *.gguf present under models/ (so a freshly downloaded model works without
# renaming). Override explicitly with MODEL=/path/to/model.gguf.
MODEL ?= $(or $(wildcard $(CURDIR)/models/model.gguf),$(firstword $(wildcard $(CURDIR)/models/*.gguf)))
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
COMPILER               ?= gcc

# unikraft 0.21.0 is validated against GCC 11-14, whose defaults are -std=gnu17
# (C) and -std=gnu++17 (C++). GCC 15+ switched the C default to gnu23, where an
# empty parameter list () means (void); unikraft's __init_array constructor
# call (boot.c) and several other TUs rely on the older unspecified-args
# semantics. Pin the standards globally and add -fpermissive so the newer
# compiler's stricter C++ diagnostics (e.g. int->enum in the arm64 PAL except
# headers) degrade to warnings instead of hard errors. These UK_* variables are
# the upstream-sanctioned injection points (appended last, see unikraft
# Makefile), so no core files need patching.
UK_CFLAGS              ?= -std=gnu17
UK_CXXFLAGS            ?= -std=gnu++17 -fpermissive

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE
export SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB
export ARCH KRAFT_TARGET EXTERNAL_DEPS_DIR VOGUE_MARCH COMPILER
export UK_CFLAGS UK_CXXFLAGS

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
	  'Build/run:  llama-{cpu,vk}-{bench,server}-{build,run}' \
	  'Deps:       deps deps-status deps-refresh' \
	  'Baseline:   linux-guest-vk-baseline' \
	  'Cleanup:    clean'

verify: test-fast venus-check vulkan-check \
	llama-cpu-bench-run llama-cpu-server-run llama-vk-bench-run llama-vk-server-run \
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
