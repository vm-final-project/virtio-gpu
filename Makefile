# SPDX-License-Identifier: BSD-3-Clause
#
# VOGUE — concise top-level test/build orchestrator

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

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB

.PHONY: help \
	test-fast test-native test-qemu test-gpu verify \
	native-tests test-core test-compat test-venus test-dispatch proto-abi vulkan-tests \
	venus-check vulkan-check eval eval-check current-stage-check \
	llm-server-vk-check llm-server-vk-throughput-check linux-guest-vk-baseline \
	kmscube-build \
	llama-cmake llama-cmake-vk llama-cmake-vk-server \
	llama-cpu-build llama-cpu-run \
	llama-cpu-server-build llama-cpu-server-run \
	llama-vk-build llama-vk-run \
	llama-vk-server-build llama-vk-server-run \
	clean

help:
	@printf '%s\n' \
	  'VOGUE concise targets' \
	  '' \
	  'Canonical test surface:' \
	  '  make native-tests' \
	  '  make test-core' \
	  '  make test-compat' \
	  '  make test-venus' \
	  '  make test-dispatch' \
	  '  make proto-abi' \
	  '  make vulkan-tests' \
	  '' \
	  'Canonical evidence surface:' \
	  '  make venus-check' \
	  '  make vulkan-check' \
	  '  make llm-server-vk-check' \
	  '  make llm-server-vk-throughput-check' \
	  '  make eval-check' \
	  '  make current-stage-check' \
	  '' \
	  'Build/run helpers:' \
	  '  make kmscube-build' \
	  '  make llama-cpu-build / -run' \
	  '  make llama-cpu-server-build / -run' \
	  '  make llama-vk-build / -run' \
	  '  make llama-vk-server-build / -run' \
	  '  make linux-guest-vk-baseline' \
	  '' \
	  'Aggregate gates:' \
	  '  make test-fast' \
	  '  make test-native' \
	  '  make test-qemu' \
	  '  make test-gpu' \
	  '  make verify' \
	  '' \
	  'Housekeeping:' \
	  '  make clean'

test-fast: native-tests proto-abi

test-native: native-tests proto-abi

test-qemu: venus-check llm-server-vk-check

test-gpu: vulkan-tests vulkan-check test-dispatch

verify: test-fast test-qemu test-gpu eval-check current-stage-check llm-server-vk-throughput-check

native-tests:
	$(MAKE) -C tests native

test-core:
	$(MAKE) -C tests test-core

test-compat:
	$(MAKE) -C tests test-compat

test-venus:
	$(MAKE) -C tests test-venus

test-dispatch:
	$(MAKE) -C tests test-dispatch

proto-abi:
	$(MAKE) -C tests proto-abi

vulkan-tests:
	$(MAKE) -C tests vulkan

venus-check:
	python3 scripts/venus_qemu_probe.py --mode 2d --allow-blocked
	python3 scripts/venus_qemu_probe.py --mode venus-ring --allow-blocked
	python3 scripts/venus_perf_eval.py --repetitions 5 --allow-blocked

vulkan-check:
	python3 scripts/vulkan_perf_eval.py --repetitions 3 --allow-blocked

eval:
	python3 scripts/eval_matrix.py --check

eval-check: venus-check vulkan-check
	python3 scripts/eval_matrix.py --check

current-stage-check:
	python3 scripts/current_stage_report.py --check

llm-server-vk-check: llama-vk-server-run
	python3 scripts/llm_server_vk_check.py --check

llm-server-vk-throughput-check:
	python3 scripts/llm_server_vk_throughput_check.py --check

linux-guest-vk-baseline:
	python3 scripts/linux_guest_vulkan_baseline.py

kmscube-build:
	mkdir -p results/kmscube_vgpu_gl/run
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target qemu/x86_64 --kraftfile Kraftfile .

llama-cmake:
	@test -n "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT not set; export it or pass make LLAMA_ROOT=/path/to/llama.cpp"; exit 2; }
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	mkdir -p results/llama
	cd $(LLAMA_ROOT) && cmake -S . -B build-unikraft-cpu \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=OFF > $(CURDIR)/results/llama/llama_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-cpu \
	    --target ggml-base ggml-cpu llama -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/llama_cmake.log 2>&1

llama-cmake-vk:
	@test -n "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT not set; export it or pass make LLAMA_ROOT=/path/to/llama.cpp"; exit 2; }
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	mkdir -p results/llama
	cd $(LLAMA_ROOT) && cmake -S . -B build-unikraft-vk \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=OFF -DGGML_VULKAN=ON \
	    $(if $(VULKAN_HEADERS_INCLUDE),-DVulkan_INCLUDE_DIR=$(VULKAN_HEADERS_INCLUDE)) \
	    -DVulkan_LIBRARY=$(VK_LIB) \
	    >> $(CURDIR)/results/llama/llama_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk \
	    --target ggml-base ggml-cpu ggml-vulkan llama -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/llama_cmake.log 2>&1

llama-cmake-vk-server:
	@test -n "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT not set; export it or pass make LLAMA_ROOT=/path/to/llama.cpp"; exit 2; }
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	mkdir -p results/llama
	cd $(LLAMA_ROOT) && cmake -S . -B build-unikraft-vk-server \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=ON \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=ON -DGGML_VULKAN=ON \
	    $(if $(VULKAN_HEADERS_INCLUDE),-DVulkan_INCLUDE_DIR=$(VULKAN_HEADERS_INCLUDE)) \
	    -DVulkan_LIBRARY=$(VK_LIB) \
	    >> $(CURDIR)/results/llama/llama_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk-server \
	    --target ggml-base ggml-cpu ggml-vulkan llama llama-server-impl -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/llama_cmake.log 2>&1

llama-cpu-build: llama-cmake
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-cpu .

llama-cpu-run: llama-cpu-build
	python3 scripts/llama_cpu_real_run.py

llama-cpu-server-build: llama-cmake
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-cpu-server .

llama-cpu-server-run: llama-cpu-server-build
	python3 scripts/llama_server_cpu_capture.py

llama-vk-build: llama-cmake-vk
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-vk .
	python3 scripts/llama_vk_build_capture.py

llama-vk-run: llama-vk-build
	python3 scripts/llama_vulkan_eval.py llama-vk

llama-vk-server-build: llama-cmake-vk-server
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-vk-server .

llama-vk-server-run: llama-vk-server-build
	python3 scripts/llama_server_vk_capture.py

clean:
	$(MAKE) -C tests clean
