LLAMA_VK_SHADER_STAMP := $(LLAMA_ROOT)/build-unikraft-vk-$(ARCH)/ggml/src/ggml-vulkan/.vogue-shaders

# Upstream ggml-vulkan locates SPIR-V headers with `find_package(SPIRV-Headers
# CONFIG REQUIRED)`, which needs an exported SPIRV-HeadersConfig.cmake. The
# vendored SPIRV-Headers checkout is raw source, so we configure+install it
# (header-only, fast) into this prefix and add it to CMAKE_PREFIX_PATH below.
SPIRV_HEADERS_SRC     := $(realpath $(EXTERNAL_DEPS_DIR)/SPIRV-Headers)
SPIRV_HEADERS_PREFIX  := $(LLAMA_ROOT)/build-spirv-headers-$(ARCH)

# Kraftfiles use @@UNIKRAFT_LOCAL@@ as a placeholder so the path is never
# relative to the Kraftfile's own directory.  This macro substitutes it with
# an absolute path derived from CURDIR and writes a resolved copy to
# .kraft-gen/ before invoking kraft build.
KRAFT_GEN_DIR := $(CURDIR)/.kraft-gen
UNIKRAFT_LOCAL ?= $(CURDIR)/.deps/src/unikraft

# PATH override is applied directly in the recipe shell (not just via the
# root Makefile's export) so KraftKit's unikraft sub-make reliably sees GNU
# make >= 4.1 on macOS regardless of how the recipe environment is inherited.
define kraft_build
	mkdir -p $(KRAFT_GEN_DIR) && \
	sed \
		-e 's|@@UNIKRAFT_LOCAL@@|$(UNIKRAFT_LOCAL)|g' \
		-e 's|@@VOGUE_SMP@@|$(or $(VOGUE_SMP),1)|g' \
		$(1) > $(KRAFT_GEN_DIR)/$(notdir $(1)) && \
	PATH="$(if $(GNUBIN),$(GNUBIN):,)$$PATH" $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile $(KRAFT_GEN_DIR)/$(notdir $(1)) .
endef

.PHONY: llama-vk-prepare \
	llama-cpu-bench-build llama-cpu-bench-run llama-cpu-server-build llama-cpu-server-run \
	llama-cpu-upstream-bench-build llama-cpu-upstream-bench-run \
	llama-vk-bench-build llama-vk-bench-run llama-vk-server-build llama-vk-server-run \
	llama-vk-upstream-bench-build llama-vk-upstream-bench-run

$(LLAMA_VK_SHADER_STAMP):
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	@test -n "$(VULKAN_HEADERS_INCLUDE)" || { echo "VULKAN_HEADERS_INCLUDE is not set"; exit 2; }
	@test -d "$(SPIRV_HEADERS_SRC)" || { echo "SPIRV-Headers source not found at $(SPIRV_HEADERS_SRC); run 'make deps'."; exit 2; }
	@test -n "$(GLSLC)" && test -x "$(GLSLC)" || { echo "blocked:missing-glslc GLSLC=/path/to/glslc hint=install-shaderc"; exit 2; }
	@test -f "$(VK_LIB)" || { echo "blocked:missing-vulkan-library VK_LIB=$(VK_LIB)"; exit 2; }
	@tmp="$${TMPDIR:-/tmp}/vogue-march-check-$$$$.o"; \
	printf 'int vogue_march_check;\n' | clang -fPIC -march=$(VOGUE_MARCH) -mtune=$(VOGUE_MTUNE) -x c -c - -o "$$tmp" >/dev/null 2>&1 || { \
		echo "blocked:unsupported-vogue-march VOGUE_MARCH=$(VOGUE_MARCH) VOGUE_MTUNE=$(VOGUE_MTUNE) compiler=clang"; \
		rm -f "$$tmp"; exit 2; \
	}; \
	rm -f "$$tmp"
	cmake -S $(SPIRV_HEADERS_SRC) -B $(SPIRV_HEADERS_PREFIX)/_build \
	    -DCMAKE_INSTALL_PREFIX=$(SPIRV_HEADERS_PREFIX) -DCMAKE_BUILD_TYPE=Release
	cmake --install $(SPIRV_HEADERS_PREFIX)/_build
	cd $(LLAMA_ROOT) && ARCH=$(ARCH) VOGUE_MARCH=$(VOGUE_MARCH) VOGUE_MTUNE=$(VOGUE_MTUNE) cmake -S . -B build-unikraft-vk-$(ARCH) \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DCMAKE_PREFIX_PATH=$(SPIRV_HEADERS_PREFIX) \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=OFF -DGGML_VULKAN=ON \
	    -DVulkan_INCLUDE_DIR=$(VULKAN_HEADERS_INCLUDE) -DVulkan_LIBRARY=$(VK_LIB) \
	    -DVulkan_GLSLC_EXECUTABLE=$(GLSLC)
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk-$(ARCH) \
	    --target ggml-vulkan -j$(LLAMA_BUILD_JOBS)
	@touch $@

llama-vk-prepare: $(LLAMA_VK_SHADER_STAMP)

llama-cpu-bench-build:
	rm -f .config.vogue-llama-cpu_$(subst /,-,$(KRAFT_TARGET)) .unikraft/build/config .unikraft/build/kconfig/auto.conf
	$(call kraft_build,kraft/Kraftfile.llama-cpu)

llama-cpu-bench-run: llama-cpu-bench-build
	python3 scripts/app-llama-cpu.py --arch "$(ARCH)" --mode bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"

llama-cpu-upstream-bench-build:
	rm -f .config.vogue-llama-cpu-upstream-bench_$(subst /,-,$(KRAFT_TARGET)) .unikraft/build/config .unikraft/build/kconfig/auto.conf
	$(call kraft_build,kraft/Kraftfile.llama-cpu-upstream-bench)

llama-cpu-upstream-bench-run: llama-cpu-upstream-bench-build
	python3 scripts/app-llama-cpu.py --arch "$(ARCH)" --mode bench --bench-kind upstream_llama_bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"

llama-cpu-server-build:
	rm -f .config.vogue-llama-cpu-server_$(subst /,-,$(KRAFT_TARGET)) .unikraft/build/config .unikraft/build/kconfig/auto.conf
	$(call kraft_build,kraft/Kraftfile.llama-cpu-server)

llama-cpu-server-run: llama-cpu-server-build
	python3 scripts/app-llama-cpu.py --arch "$(ARCH)" --mode server --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"

llama-vk-bench-build: llama-vk-prepare
	$(call kraft_build,kraft/Kraftfile.llama-vk)

llama-vk-bench-run: llama-vk-bench-build
	python3 scripts/app-llama-vk.py --arch "$(ARCH)" --mode bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"

llama-vk-upstream-bench-build: llama-vk-prepare
	$(call kraft_build,kraft/Kraftfile.llama-vk-upstream-bench)

llama-vk-upstream-bench-run: llama-vk-upstream-bench-build
	python3 scripts/app-llama-vk.py --arch "$(ARCH)" --mode bench --bench-kind upstream_llama_bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"

llama-vk-server-build: llama-vk-prepare
	$(call kraft_build,kraft/Kraftfile.llama-vk-server)

llama-vk-server-run: llama-vk-server-build
	python3 scripts/app-llama-vk.py --arch "$(ARCH)" --mode server --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)" --smp "$(or $(VOGUE_SMP),1)"
