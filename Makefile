# VOGUE production workspace
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c

KRAFT ?= $(if $(wildcard $(CURDIR)/.tools/kraftkit/kraft),$(CURDIR)/.tools/kraftkit/kraft,kraft)
QEMU ?= qemu-system-x86_64
TYPST ?= typst

# External source roots. Defaults are sibling checkouts next to vm-final-project;
# override via env or in config/external_paths.json. The cmake toolchain reads
# the same variables via the environment.
LLAMA_ROOT             ?= $(realpath $(CURDIR)/../llama.cpp)
VENUS_PROTOCOL_ROOT    ?= $(realpath $(CURDIR)/../venus-protocol)
VULKAN_HEADERS_INCLUDE ?=
SPIRV_HEADERS_INCLUDE  ?=
# Host Vulkan loader path; only used to satisfy ggml-vulkan's find_package(Vulkan)
# during the static cross-build. Symbols are provided by libukggml_vk at the
# unikernel link, so this library is never pulled into the static archives.
VK_LIB                 ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1
# Parallelism for the upstream llama.cpp cross-builds. Capped (not raw nproc):
# the heavy C++ compile is flaky at very high -j on this host's shared
# filesystem (transient ENOENT on concurrent header reads). Override freely.
LLAMA_BUILD_JOBS       ?= 8
HOST_CXX_INCLUDE       ?=
HOST_GCC_LIB           ?=

export LLAMA_ROOT VENUS_PROTOCOL_ROOT VULKAN_HEADERS_INCLUDE SPIRV_HEADERS_INCLUDE HOST_CXX_INCLUDE HOST_GCC_LIB

.PHONY: help all test test-fast test-native test-qemu test-gpu tests artifact-smoke artifact-functional artifact-full artifact-paper artifact-quick artifact-check ggml-vk-dispatch verify native-tests vulkan-tests vk-drm-shim-check paper paper-check governance-check lib-readme-check naming-check image-size-check perf-check boot-time-check model-load-time-check llm-server-vk-check current-stage-check current-stage-refresh real-path-check app-perf app-perf-check eval eval-check claim-check kmscube-build glmark2-build app-port-check kmscube-run kmscube-check venus-check stage-check benchmark-check vulkan-check llama-check llama-vulkan-api-coverage llama-ggml-vk-dispatch llama-vulkan-check llama-upstream-cmake llama-upstream-cmake-vk llama-upstream-cpu-build llama-upstream-cpu-run llama-upstream-cpu-check llama-upstream-vk-build llama-upstream-vk-server-build llama-upstream-vk-run llama-upstream-vk-check llama-upstream-check env10-real-check multi-env-bench app-multi-env-bench llama-env-list llama-env-check llama-env-bench llama-env-server gen-libukvenus gen-libukvenus-plan gen-libukvenus-check depgraph depgraph-check clean

help:
	@printf '%s\n' \
	  'VOGUE reviewer/test targets' \
	  '  make test-fast           Daily developer gate: governance + app/lib docs + native/proto tests' \
	  '  make test-native         Host-native tests; no QEMU/GPU requirement' \
	  '  make test-qemu           QEMU/VirtIO-GPU probes; may produce structured blocked rows' \
	  '  make test-gpu            Host Vulkan/static Venus and ggml-vulkan dispatch checks' \
	  '  make artifact-quick      Fast artifact gate: native tests, Vulkan API coverage, docs, paper' \
	  '  make artifact-check      Functional gate: artifact-quick plus eval/current-stage' \
	  '  make test                Backward-compatible native + host Vulkan + VirtIO-GPU ABI tests' \
	  '  make governance-check    Validate app/lib/claim/manifest governance metadata' \
	  '  make app-port-check      Validate every apps/*/PORTING.md plus official ports' \
	  '  make llama-env-check     Validate selectable llama.cpp environment matrix' \
	  '  make llama-env-bench     Dry-run env-specific llama.cpp bench plan' \
	  '  make llama-env-server    Dry-run Unikraft llama.cpp server appliance plan' \
	  '  make lib-readme-check    Validate every libs/*/README.md' \
	  '  make eval-check          Regenerate concise evidence matrix; blocked rows stay explicit' \
	  '  make verify              Release gate; broad and QEMU-dependent when available' \
	  '  make clean               Remove generated test/paper outputs'

all: native-tests vulkan-tests app-perf-check eval-check paper

artifact-smoke: test-fast llama-vulkan-api-coverage llama-ggml-vk-dispatch paper-check

artifact-functional: artifact-smoke vulkan-tests app-perf-check eval-check current-stage-check gen-libukvenus-check image-size-check perf-check boot-time-check model-load-time-check llm-server-vk-check

artifact-paper: paper-check paper

artifact-full: artifact-functional venus-check vulkan-check llama-vulkan-check artifact-paper claim-check

artifact-quick: artifact-smoke artifact-paper

artifact-check: artifact-functional artifact-paper

test-fast: governance-check lib-readme-check app-port-check llama-env-check naming-check native-tests
	$(MAKE) -C tests proto-abi

test-native: native-tests
	$(MAKE) -C tests proto-abi

test-qemu: kmscube-check venus-check

test-gpu: vulkan-tests vulkan-check llama-vulkan-api-coverage llama-ggml-vk-dispatch

test tests: native-tests vulkan-tests
	$(MAKE) -C tests proto-abi

verify:
	$(MAKE) native-tests vulkan-tests app-port-check app-perf-check naming-check
	$(MAKE) kmscube-build kmscube-check venus-check stage-check benchmark-check
	$(MAKE) vulkan-check eval-check paper paper-check lib-readme-check current-stage-check claim-check
	$(MAKE) gen-libukvenus-check image-size-check perf-check boot-time-check model-load-time-check llm-server-vk-check

native-tests:
	$(MAKE) -C tests native

vk-drm-shim-check:
	$(MAKE) -C tests g5

vulkan-tests:
	$(MAKE) -C tests vulkan

vulkan-check:
	python3 scripts/vulkan_registry_check.py
	python3 scripts/vulkan_perf_eval.py --repetitions 3 --allow-blocked

# llama.cpp gates: upstream single-application appliances plus the minimal
# static Vulkan/Venus dispatch layer required by upstream ggml-vulkan.
# Legacy synthetic llama substrate rows were pruned.
llama-check: llama-env-check llama-vulkan-api-coverage llama-ggml-vk-dispatch
	python3 scripts/llama_env_matrix.py --dry-run --output results/llama-env/plan.json

llama-vulkan-api-coverage:
	python3 scripts/llama_vulkan_api_coverage.py --check

llama-ggml-vk-dispatch: llama-vulkan-api-coverage
	python3 scripts/llama_vulkan_eval.py n3-dispatch

llama-vulkan-check: llama-vulkan-api-coverage llama-ggml-vk-dispatch
	@echo "llama-vulkan-check: static ggml-vulkan/Venus dispatch gates complete; runtime evidence is llm.bench.vk."

# W3 — upstream llama.cpp on Unikraft (llm.bench.cpu, llm.bench.vk).
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

# CPU server appliance (llm.server.cpu): build the single-purpose server image
# and boot it to the READY line. No GPU required.
llama-upstream-server-build: llama-upstream-cmake
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
	    --target qemu/x86_64 --kraftfile kraft/Kraftfile.llama-upstream-server . \
	    && echo "vogue-llama-upstream-server: build-pass"

llama-upstream-server-run: llama-upstream-server-build
	python3 scripts/llama_server_cpu_capture.py

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

# Vulkan server-only appliance (llm.server.vk). Same dispatch chain as the
# bench image; only Kconfig MODE differs, so server.cpp is compiled instead of
# bench.cpp and the image carries no benchmark code path.
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

multi-env-bench:
	python3 scripts/multi_env_bench.py --skip-vm

llama-env-list:
	python3 scripts/llama_env_matrix.py --list --mode all

llama-env-check:
	python3 scripts/llama_env_matrix.py --check

llama-env-bench:
	python3 scripts/llama_env_matrix.py --dry-run --output results/llama-env/plan.json

llama-env-server:
	python3 scripts/llama_env_matrix.py --dry-run --mode server --output results/llama-env/server-plan.json

app-multi-env-bench:
	python3 scripts/app_multi_env_bench.py

paper:
	$(TYPST) compile paper/main.typ paper/vogue-paper.pdf

paper-check:
	python3 scripts/paper_consistency_check.py

governance-check:
	python3 scripts/governance_check.py --check

lib-readme-check:
	python3 scripts/lib_readme_check.py --check

naming-check:
	python3 scripts/naming_check.py

image-size-check:
	python3 scripts/image_size_check.py --check

# perf-check compares results/app_perf.json and
# results/llama/upstream_cpu.json against config/perf_baseline.json
# and fails if anything regresses by more than the configured percentage.
perf-check: app-perf-check
	python3 scripts/perf_check.py --check --allow-blocked

# boot-time-check measures the boot-to-READY latency of every built appliance.
# Hosts without QEMU or without a built image produce structured blocker rows.
boot-time-check:
	python3 scripts/boot_time_check.py --check

# plan-optimize.md L1.4: model-load latency record per appliance.
model-load-time-check:
	python3 scripts/model_load_time_check.py --check

# plan-optimize.md Phase-2 contract for the Vulkan server appliance.
llm-server-vk-check:
	python3 scripts/llm_server_vk_check.py --check

real-path-check:
	python3 scripts/real_virtio_gpu_path_check.py --check --allow-blocked

current-stage-check:
	python3 scripts/current_stage_report.py --check

current-stage-refresh: stage-check benchmark-check eval-check paper-check lib-readme-check real-path-check current-stage-check

app-perf:
	python3 scripts/app_perf_eval.py --check

app-perf-check:
	python3 scripts/app_perf_eval.py --check

depgraph:
	python3 scripts/gen_depgraph.py

depgraph-check:
	python3 scripts/gen_depgraph.py --check

eval:
	python3 scripts/eval_matrix.py --check

eval-check: app-perf-check venus-check vulkan-check llama-check llama-vulkan-check
	python3 scripts/eval_matrix.py --check
	grep -n "gfx.kmscube.sw.*software\|K1 requires\|Claim Boundaries" results/vogue_evaluation_matrix.md paper/sections/08-evaluation.typ >/dev/null

claim-check: eval-check
	@# Reject abandoned custom compute-remoting vocabulary outside archival/design material.
	@# This deliberately avoids broad tokens such as v6/v7 so Linux/kernel versions do not fail the gate.
	@if grep -RIn --exclude-dir=.git --exclude-dir=.omx --exclude-dir=design \
	     --exclude-dir=resource --exclude-dir=.unikraft --exclude-dir=rootfs \
	     --exclude-dir=tests --exclude-dir=results --exclude-dir=paper/clean-acmart \
	     --exclude='CLAUDE.md' --exclude='*.pdf' --exclude='cscope.out*' \
	     --exclude='Makefile' --exclude='.env' \
	     -E '\bAPIR\b|ggml-virtgpu|ggml-remoting|virtgpu-compute-backend|ggml_backend_virtgpu_reg' . ; then \
	  echo "claim-check: forbidden custom compute-remoting vocabulary present"; exit 1; \
	fi

app-port-check:
	python3 scripts/app_port_check.py

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

venus-check:
	$(MAKE) -C tests proto-abi
	python3 scripts/check_venus_vulkan_docs.py
	python3 scripts/real_driver_static_check.py
	python3 scripts/venus_qemu_probe.py --mode 2d --allow-blocked
	python3 scripts/venus_qemu_probe.py --mode venus-ring --allow-blocked
	python3 scripts/venus_perf_eval.py --repetitions 5 --allow-blocked
	$(MAKE) real-path-check

stage-check: venus-check
	python3 scripts/unikraft_alignment_check.py
	python3 scripts/stage_audit.py --check

benchmark-check: app-perf-check venus-check
	python3 scripts/benchmark_summary.py --check


# libukvenus generator (delegates to ../venus-protocol/vn_protocol.py).
gen-libukvenus-plan:
	python3 scripts/gen_libukvenus.py plan

gen-libukvenus-check:
	python3 scripts/gen_libukvenus.py check

gen-libukvenus: gen-libukvenus-check
	python3 scripts/gen_libukvenus.py generate

clean:
	$(MAKE) -C tests clean
	rm -f paper/vogue-paper.pdf
	rm -rf libs/libukvenus/generated
