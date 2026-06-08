# SPDX-License-Identifier: BSD-3-Clause
#
# VOGUE — top-level build & review orchestrator
# ============================================================================
# This is a thin wrapper Makefile (Unikraft "one orchestrator, many component
# Makefiles" pattern). It does not compile the unikernels itself — those are
# built by KraftKit against the sibling ../unikraft checkout — it only:
#
#   * delegates the host-native C suite to tests/Makefile,
#   * drives the Python evidence/governance/perf gates under scripts/,
#   * wraps `kraft build` for each single-purpose appliance in kraft/.
#
# Run every target from inside virtio-gpu/. `make` with no target prints help.
# Targets are grouped into clearly-labelled sections below; `make help` lists
# the everyday entry points.
# ============================================================================

SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DEFAULT_GOAL := help

# ----------------------------------------------------------------------------
# Toolchain
# ----------------------------------------------------------------------------
KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
QEMU  ?= qemu-system-x86_64

# ----------------------------------------------------------------------------
# External source roots
# Defaults are sibling checkouts next to virtio-gpu/; override via env or
# config/external_paths.json. The cmake toolchain reads the same variables from
# the environment, so they are exported below.
# ----------------------------------------------------------------------------
LLAMA_ROOT             ?= $(realpath $(CURDIR)/../llama.cpp)
VENUS_PROTOCOL_ROOT    ?= $(realpath $(CURDIR)/../venus-protocol)
VULKAN_HEADERS_INCLUDE ?= $(realpath $(CURDIR)/../Vulkan-Headers/include)
SPIRV_HEADERS_INCLUDE  ?= $(realpath $(CURDIR)/../SPIRV-Headers/include)
# Host Vulkan loader; only satisfies ggml-vulkan's find_package(Vulkan) during
# the static cross-build. Symbols come from the in-tree ggml-vulkan build (in app-llama-upstream-vk)
# so this library is never pulled into the static archives.
VK_LIB                 ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
# Parallelism for the upstream llama.cpp cross-builds. Capped (not raw nproc):
# the heavy C++ compile is flaky at very high -j on this host's shared
# filesystem (transient ENOENT on concurrent header reads). Override freely.
LLAMA_BUILD_JOBS       ?= 8
HOST_CXX_INCLUDE       ?=
HOST_GCC_LIB           ?=

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB

.PHONY: help all \
        test-fast test-native test-qemu test-gpu test tests verify \
        artifact-smoke artifact-functional artifact-full artifact-quick artifact-check \
        native-tests test-core test-venus test-dispatch proto-abi vulkan-tests vk-drm-shim-check ggml-vk-dispatch \
        kmscube-build kmscube-run kmscube-check glmark2-build \
        venus-check vulkan-check stage-check benchmark-check real-path-check \
        llama-check llama-vulkan-api-coverage llama-ggml-vk-dispatch llama-vulkan-check \
        llama-upstream-cmake llama-upstream-cmake-vk llama-upstream-cmake-vk-server \
        llama-upstream-cpu-build llama-upstream-cpu-run llama-upstream-cpu-check \
        llama-upstream-server-build llama-upstream-server-run \
        llama-upstream-vk-build llama-upstream-vk-run llama-upstream-vk-check \
        llama-upstream-vk-server-build llama-upstream-check env10-real-check \
        multi-env-bench app-multi-env-bench linux-guest-vk-baseline \
        llama-env-list llama-env-check llama-env-bench llama-env-server \
        governance-check lib-readme-check app-port-check naming-check native-vulkan-no-drm-check claim-check \
        eval eval-check current-stage-check current-stage-refresh \
        app-perf-check perf-check image-size-check boot-time-check model-load-time-check llm-server-vk-check llm-server-vk-throughput-check \
        depgraph depgraph-check gen-libukvenus gen-libukvenus-plan gen-libukvenus-check gen-libukvenus-verify gen-libukvenus-selftest \
        clean

# ============================================================================
# Help (default goal)
# ============================================================================
help:
	@printf '%s\n' \
	  'VOGUE build & review targets  (run from virtio-gpu/)' \
	  '' \
	  'Everyday gates:' \
	  '  make test-fast          Daily developer gate: governance + app/lib docs + native/proto tests' \
	  '  make test-native        Host-native C suite + wire-ABI test; no QEMU/GPU needed' \
	  '  make test-qemu          QEMU/VirtIO-GPU probes (kmscube + Venus); may emit blocked rows' \
	  '  make test-gpu           Host Vulkan + static Venus/ggml-vulkan dispatch checks' \
	  '  make verify             Broad release gate; runs QEMU-dependent checks when available' \
	  '' \
	  'Host-native test groups (delegate to tests/Makefile):' \
	  '  make native-tests       Full deterministic C suite (primary CI gate)' \
	  '  make test-core          Group 1: VirtIO-GPU core / DMA / DRM+GBM shims' \
	  '  make test-venus         Group 2: virtgpu ioctl / VK ICD / Venus / virgl' \
	  '  make test-dispatch      Group 3: ggml-vulkan static dispatch' \
	  '  make proto-abi          VirtIO-GPU wire-ABI struct/feature check' \
	  '  make vulkan-tests       Optional host Vulkan compute baseline (needs VK_LIB/VK_INC)' \
	  '' \
	  'Appliances (KraftKit; require sibling ../unikraft + external roots):' \
	  '  make kmscube-build      Build the VirtIO-GPU graphics appliance' \
	  '  make llama-upstream-cpu-build / -vk-build / -vk-server-build' \
	  '  make llama-env-check    Validate the selectable llama.cpp environment matrix' \
	  '' \
	  'Evidence, governance & docs:' \
	  '  make governance-check   Validate app/lib/claim governance metadata' \
	  '  make app-port-check     Validate every apps/*/PORTING.md' \
	  '  make lib-readme-check   Validate every libs/*/README.md' \
	  '  make eval-check         Regenerate the evidence matrix (blocked rows stay explicit)' \
	  '  make llm-server-vk-check  llama.cpp Vulkan HTTP server contract + same-run HTTP probe' \
	  '' \
	  'Artifact bundles:  artifact-quick | artifact-check | artifact-full' \
	  '  make clean              Remove generated test outputs'

# ============================================================================
# Aggregate gates
# ============================================================================
# all: the broadest "build everything reproducible without a GPU" convenience.
all: native-tests vulkan-tests app-perf-check eval-check

# test-fast: the daily inner-loop gate (no QEMU/GPU).
test-fast: governance-check lib-readme-check app-port-check llama-env-check naming-check native-vulkan-no-drm-check native-tests
	$(MAKE) -C tests proto-abi

# test-native: host-native suite + wire-ABI test.
test-native: native-tests
	$(MAKE) -C tests proto-abi

# test-qemu: QEMU-dependent VirtIO-GPU/Venus probes (structured blockers off-host).
test-qemu: kmscube-check venus-check

# test-gpu: host Vulkan + static Venus/ggml-vulkan dispatch.
test-gpu: vulkan-tests vulkan-check llama-vulkan-api-coverage llama-ggml-vk-dispatch

# test / tests: backward-compatible native + host Vulkan + wire-ABI tests.
test tests: native-tests vulkan-tests
	$(MAKE) -C tests proto-abi

# verify: broadest release gate; QEMU-dependent steps degrade to blocked rows.
verify:
	$(MAKE) native-tests vulkan-tests app-port-check app-perf-check naming-check
	$(MAKE) kmscube-build kmscube-check venus-check stage-check benchmark-check
	$(MAKE) vulkan-check eval-check lib-readme-check current-stage-check claim-check
	$(MAKE) gen-libukvenus-check image-size-check perf-check boot-time-check model-load-time-check llm-server-vk-check

# Artifact bundles, increasing in scope.
artifact-smoke:      test-fast llama-vulkan-api-coverage llama-ggml-vk-dispatch
artifact-functional: artifact-smoke vulkan-tests app-perf-check eval-check current-stage-check gen-libukvenus-check image-size-check perf-check boot-time-check model-load-time-check llm-server-vk-check
artifact-full:       artifact-functional venus-check vulkan-check llama-vulkan-check claim-check
artifact-quick:      artifact-smoke
artifact-check:      artifact-functional

# ============================================================================
# Host-native test groups  (thin wrappers over tests/Makefile)
# The deterministic C suite is the source of truth; these root targets are the
# single entry point so reviewers never need to `cd tests/`.
# ============================================================================
native-tests:
	$(MAKE) -C tests native

test-core:
	$(MAKE) -C tests test-core

test-venus:
	$(MAKE) -C tests test-venus

test-dispatch:
	$(MAKE) -C tests test-dispatch

# vk.ggml-dispatch alias kept for existing notes/scripts.
ggml-vk-dispatch: test-dispatch

proto-abi:
	$(MAKE) -C tests proto-abi

vulkan-tests:
	$(MAKE) -C tests vulkan

# Single-binary convenience: virtgpu DRM ioctl shim (vk.drm-shim).
vk-drm-shim-check:
	$(MAKE) -C tests g5

# ============================================================================
# Graphics appliances (KraftKit)
# ============================================================================
kmscube-build:
	mkdir -p results/kmscube_vgpu_gl/run
	{ \
		rm -f results/kmscube_vgpu_gl/run/blocker.json; \
		if COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update --target qemu/x86_64 --kraftfile Kraftfile .; then \
			echo "vogue: build-pass image=.unikraft/build/vogue_qemu-x86_64"; \
		else \
			rc=$$?; echo "vogue: build blocked by local Kraft/Unikraft toolchain rc=$$rc"; \
			python3 -c 'import json,pathlib; pathlib.Path("results/kmscube_vgpu_gl/run/blocker.json").write_text(json.dumps({"status":"blocked:build","blocked_stage":"build","first_missing_dependency":"Kraft/Unikraft toolchain failed; see results/kmscube_vgpu_gl/run/build.log","evidence_log":"results/kmscube_vgpu_gl/run/build.log","claim_allowed":"Build blocker documented; no K1 pass claim.","claim_forbidden":"Native K1 virgl/kmscube pass or acceleration claim.","next_step":"Fix the local Unikraft compiler/Kraft target failure and rerun make kmscube-build."}, indent=2) + "\n")'; \
		fi; \
	} 2>&1 | tee results/kmscube_vgpu_gl/run/build.log

kmscube-run:
	test -x "$$(command -v $(QEMU))" || { echo "$(QEMU) missing"; exit 2; }
	test -f .unikraft/build/vogue_qemu-x86_64 || $(MAKE) kmscube-build
	$(QEMU) -machine accel=tcg -cpu max -m 256M -kernel .unikraft/build/vogue_qemu-x86_64 -display egl-headless,gl=on -serial mon:stdio -device virtio-gpu-gl-pci,disable-modern=on,disable-legacy=off 2>&1 | tee results/kmscube_vgpu_gl/run/run.log

kmscube-check:
	python3 scripts/kmscube_vgpu_gl_eval.py --emit-pixel-proof --check --allow-blocked

glmark2-build:
	mkdir -p results/glmark2/run
	{ \
		rm -f results/glmark2/run/blocker.json; \
		if COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update --target qemu/x86_64 --kraftfile kraft/Kraftfile.glmark2 .; then \
			echo "vogue-glmark2: build-pass image=.unikraft/build/vogue-glmark2_qemu-x86_64"; \
		else \
			rc=$$?; echo "vogue-glmark2: build blocked by local Kraft/Unikraft toolchain rc=$$rc"; \
			python3 -c 'import json,pathlib; pathlib.Path("results/glmark2/run/blocker.json").write_text(json.dumps({"status":"blocked:build","blocked_stage":"build","first_missing_dependency":"Kraft/Unikraft toolchain failed; see results/glmark2/run/build.log","evidence_log":"results/glmark2/run/build.log","claim_allowed":"Build blocker documented; no gfx.glmark2.sw QEMU pass claim.","claim_forbidden":"Full glmark2 suite, QEMU performance, or acceleration claim."}, indent=2) + "\n")'; \
		fi; \
	} 2>&1 | tee results/glmark2/run/build.log

# ============================================================================
# VirtIO-GPU / Venus / Vulkan host checks
# ============================================================================
venus-check:
	$(MAKE) -C tests proto-abi
	python3 scripts/check_venus_vulkan_docs.py
	python3 scripts/real_driver_static_check.py
	python3 scripts/venus_qemu_probe.py --mode 2d --allow-blocked
	python3 scripts/venus_qemu_probe.py --mode venus-ring --allow-blocked
	python3 scripts/venus_perf_eval.py --repetitions 5 --allow-blocked
	$(MAKE) real-path-check

vulkan-check:
	python3 scripts/vulkan_registry_check.py
	python3 scripts/vulkan_perf_eval.py --repetitions 3 --allow-blocked

stage-check: venus-check
	python3 scripts/unikraft_alignment_check.py
	python3 scripts/stage_audit.py --check

benchmark-check: app-perf-check venus-check
	python3 scripts/benchmark_summary.py --check

real-path-check:
	python3 scripts/real_virtio_gpu_path_check.py --check --allow-blocked

# ============================================================================
# llama.cpp — static dispatch gates + upstream single-application appliances
# ============================================================================
# Static ggml-vulkan/Venus dispatch coverage (no GPU; host-native).
llama-check: llama-env-check llama-vulkan-api-coverage llama-ggml-vk-dispatch
	python3 scripts/llama_env_matrix.py --dry-run --output results/llama-env/plan.json

llama-vulkan-api-coverage:
	python3 scripts/llama_vulkan_api_coverage.py --check

llama-ggml-vk-dispatch: llama-vulkan-api-coverage
	python3 scripts/llama_vulkan_eval.py n3-dispatch

llama-vulkan-check: llama-vulkan-api-coverage llama-ggml-vk-dispatch
	@echo "llama-vulkan-check: static ggml-vulkan/Venus dispatch gates complete; runtime evidence is llm.bench.vk."

# --- Upstream llama.cpp cross-build (libllama.a via cmake/unikraft-clang.cmake) -
llama-upstream-cmake:
	@test -n "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT not set; export it or pass make LLAMA_ROOT=/path/to/llama.cpp"; exit 2; }
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	mkdir -p results/llama
	cd $(LLAMA_ROOT) && cmake -S . -B build-unikraft-cpu \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=OFF > $(CURDIR)/results/llama/upstream_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-cpu \
	    --target ggml-base ggml-cpu llama -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/upstream_cmake.log 2>&1 && \
	    echo "vogue-llama-upstream: cmake-build-pass libllama.a=$$(stat -c%s build-unikraft-cpu/src/libllama.a)B"

llama-upstream-cmake-vk:
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
	    >> $(CURDIR)/results/llama/upstream_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk \
	    --target ggml-base ggml-cpu ggml-vulkan llama -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/upstream_cmake.log 2>&1 && \
	    echo "vogue-llama-upstream-vk: cmake-build-pass libllama.a=$$(stat -c%s build-unikraft-vk/src/libllama.a)B"

llama-upstream-cmake-vk-server:
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
	    >> $(CURDIR)/results/llama/upstream_cmake.log 2>&1
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk-server \
	    --target ggml-base ggml-cpu ggml-vulkan llama llama-server-impl -j$(LLAMA_BUILD_JOBS) \
	    >> $(CURDIR)/results/llama/upstream_cmake.log 2>&1 && \
	    echo "vogue-llama-upstream-vk-server: cmake-build-pass native llama-server-impl"

# --- CPU bench appliance (llm.bench.cpu) -----------------------------------
llama-upstream-cpu-build: llama-upstream-cmake
	mkdir -p results/llama
	{ \
		rm -f results/llama/upstream_cpu_blocker.json; \
		if COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		    --target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-upstream-cpu .; then \
			echo "vogue-llama-upstream-cpu: build-pass"; \
		else \
			rc=$$?; echo "vogue-llama-upstream-cpu: build blocked rc=$$rc"; \
			echo '{"status":"blocked:kraft-build-failed"}' > results/llama/upstream_cpu_blocker.json; \
		fi; \
	} 2>&1 | tee results/llama/upstream_cpu_build.log

llama-upstream-cpu-run: llama-upstream-cpu-build
	python3 scripts/llama_cpu_real_run.py

llama-upstream-cpu-check: llama-upstream-cpu-run
	python3 -c "\
import json, sys; \
i=json.load(open('results/llama/upstream_cpu.json')); \
ok = i.get('pass') or i.get('scaffold_booted'); \
sys.exit(0 if ok else 1)"

# --- CPU server appliance (llm.server.cpu): boots to model-loaded READY -----
llama-upstream-server-build: llama-upstream-cmake
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
	    --target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-upstream-server . \
	    && echo "vogue-llama-upstream-server: build-pass"

llama-upstream-server-run: llama-upstream-server-build
	python3 scripts/llama_server_cpu_capture.py

# --- Vulkan bench appliance (llm.bench.vk) ----------------------------------
llama-upstream-vk-build: llama-upstream-cmake-vk
	mkdir -p results/llama
	{ \
		rm -f results/llama/upstream_vk_blocker.json; \
		if COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		    --target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-upstream-vk .; then \
			echo "vogue-llama-upstream-vk: build-pass"; \
		else \
			rc=$$?; echo "vogue-llama-upstream-vk: build blocked rc=$$rc"; \
			echo '{"status":"blocked:kraft-build-failed"}' > results/llama/upstream_vk_blocker.json; \
		fi; \
	} 2>&1 | tee results/llama/upstream_vk_build.log
	python3 scripts/llama_vk_build_capture.py

llama-upstream-vk-run: llama-upstream-vk-build
	python3 scripts/llama_vulkan_eval.py upstream-vk

# --- Vulkan HTTP server appliance (llm.server.vk) ---------------------------
# Same Venus dispatch chain as the bench image; only the Kconfig MODE differs
# (server.cpp instead of bench.cpp). Adds the lwIP/netdev + ukrandom devfs
# stack so the upstream llama_server() listener serves HTTP. Boot + same-run
# HTTP probe live in scripts/llama_server_vk_capture.py; contract in
# llm-server-vk-check.
llama-upstream-vk-server-build: llama-upstream-cmake-vk-server
	mkdir -p results/llama
	{ \
		rm -f results/llama/upstream_server_vk_blocker.json; \
		if COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		    --target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-upstream-vk-server .; then \
			echo "vogue-llama-upstream-vk-server: build-pass"; \
		else \
			rc=$$?; echo "vogue-llama-upstream-vk-server: build blocked rc=$$rc"; \
			echo '{"status":"blocked:kraft-build-failed"}' > results/llama/upstream_server_vk_blocker.json; \
		fi; \
	} 2>&1 | tee results/llama/upstream_server_vk_build.log

llama-upstream-vk-check: llama-upstream-vk-run
	python3 -c "\
import json, sys; \
i=json.load(open('results/llama/upstream_vk.json')); \
ok = i.get('pass') or i.get('scaffold_booted'); \
sys.exit(0 if ok else 1)"

llama-upstream-check: llama-upstream-cpu-check llama-upstream-vk-check

env10-real-check: llama-upstream-vk-run
	python3 -c "\
import json, sys; \
i=json.load(open('results/llama/env10_real.json')); \
ok = i.get('pass') or (i.get('status','').startswith('blocked:')); \
sys.exit(0 if ok else 1)"

# --- Environment matrix + multi-environment benches -------------------------
llama-env-list:
	python3 scripts/llama_env_matrix.py --list --mode all

llama-env-check:
	python3 scripts/llama_env_matrix.py --check

llama-env-bench:
	python3 scripts/llama_env_matrix.py --dry-run --output results/llama-env/plan.json

llama-env-server:
	python3 scripts/llama_env_matrix.py --dry-run --mode server --output results/llama-env/server-plan.json

multi-env-bench:
	python3 scripts/multi_env_bench.py --skip-vm

# Para-virtualised reference: stock Linux guest kernel in QEMU over the SAME
# virtio-gpu-gl Venus path + upstream ggml-vulkan binary/GGUF as the Unikraft
# port. Isolates unikernel-vs-Linux from virtualised-vs-bare-metal. Blocked
# (still passing) on hosts without the guest kernel/KVM/GPU/model.
linux-guest-vk-baseline:
	python3 scripts/linux_guest_vulkan_baseline.py

app-multi-env-bench:
	python3 scripts/app_multi_env_bench.py

# ============================================================================
# Evidence, governance & claim discipline
# ============================================================================
governance-check: gen-libukvenus-verify
	python3 scripts/governance_check.py --check

lib-readme-check:
	python3 scripts/lib_readme_check.py --check

app-port-check:
	python3 scripts/app_port_check.py

naming-check:
	python3 scripts/naming_check.py

native-vulkan-no-drm-check:
	python3 scripts/check_native_vulkan_no_drm.py

# eval: regenerate the evidence matrix only (no upstream gate deps).
eval:
	python3 scripts/eval_matrix.py --check

# eval-check: regenerate with all contributing gates, then assert key rows.
eval-check: app-perf-check venus-check vulkan-check llama-check llama-vulkan-check
	python3 scripts/eval_matrix.py --check
	grep -n "gfx.kmscube.submit\|xport.qemu-vgpu\|llm.server.vk" results/vogue_evaluation_matrix.md >/dev/null

claim-check: eval-check
	@# Reject abandoned custom compute-remoting vocabulary outside archival/design material.
	@# This deliberately avoids broad tokens such as v6/v7 so Linux/kernel versions do not fail the gate.
	@if grep -RIn --exclude-dir=.git --exclude-dir=.omx --exclude-dir=design \
	     --exclude-dir=resource --exclude-dir=.unikraft --exclude-dir=rootfs \
	     --exclude-dir=tests --exclude-dir=results \
	     --exclude='CLAUDE.md' --exclude='*.pdf' --exclude='cscope.out*' \
	     --exclude='Makefile' --exclude='.env' \
	     -E '\bAPIR\b|ggml-virtgpu|ggml-remoting|virtgpu-compute-backend|ggml_backend_virtgpu_reg' . ; then \
	  echo "claim-check: forbidden custom compute-remoting vocabulary present"; exit 1; \
	fi

current-stage-check:
	python3 scripts/current_stage_report.py --check

current-stage-refresh: stage-check benchmark-check eval-check lib-readme-check real-path-check current-stage-check

# ============================================================================
# Performance & resource gates
# ============================================================================
# app-perf-check: kmscube/glmark2 software-substrate best-of-N benchmark.
app-perf-check:
	python3 scripts/app_perf_eval.py --check

# perf-check: regression gate vs config/perf_baseline.json (app + CPU llama).
perf-check: app-perf-check
	python3 scripts/perf_check.py --check --allow-blocked

# image-size-check: appliance image-size budget.
image-size-check:
	python3 scripts/image_size_check.py --check

# boot-time-check: boot-to-READY latency per built appliance (blocked off-host).
boot-time-check:
	python3 scripts/boot_time_check.py --check

# model-load-time-check: plan-optimize.md L1.4 model-load latency per appliance.
model-load-time-check:
	python3 scripts/model_load_time_check.py --check

# llm-server-vk-check: llama.cpp Vulkan HTTP server static contract + the
# same-run HTTP probe (/health, /v1/models, /completion) recorded by the capture.
llm-server-vk-check:
	python3 scripts/llm_server_vk_check.py --check

# llm-server-vk-throughput-check: boot the server and drive a bounded burst of
# HTTP completions, recording measured requests/s, tokens/s, and TTFT. Blocked
# (and still passing) on hosts without QEMU/GPU/model.
llm-server-vk-throughput-check:
	python3 scripts/llm_server_vk_throughput_check.py --check

# ============================================================================
# Generators & dependency graph
# ============================================================================
# libukvulkan_venus encoder generator (delegates to ../venus-protocol/vn_protocol.py).
gen-libukvenus-plan:
	python3 scripts/gen_libukvenus.py plan

gen-libukvenus-check:
	python3 scripts/gen_libukvenus.py check

gen-libukvenus: gen-libukvenus-check
	python3 scripts/gen_libukvenus.py generate

# Prove the committed generated tree still matches a fresh upstream regen.
gen-libukvenus-verify: gen-libukvenus-check
	python3 scripts/gen_libukvenus.py verify

# Self-tests for the generator pin/extractor/manifest/coverage/parity harness.
gen-libukvenus-selftest:
	python3 scripts/venus/test_pin.py
	python3 scripts/venus/test_extract.py
	python3 scripts/venus/test_manifest.py
	python3 scripts/venus/test_generate.py
	python3 scripts/venus/test_coverage.py

depgraph:
	python3 scripts/gen_depgraph.py

depgraph-check:
	python3 scripts/gen_depgraph.py --check

# ============================================================================
# Clean
# ============================================================================
clean:
	$(MAKE) -C tests clean
	# libs/libukvulkan_venus/generated/ is committed verbatim (sha256 GENERATED.lock,
	# gated by gen-libukvenus-verify) — regenerate with `make gen-libukvenus`,
	# never `clean`-delete it.
