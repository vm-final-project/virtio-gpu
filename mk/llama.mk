LLAMA_VK_SHADER_STAMP := $(LLAMA_ROOT)/build-unikraft-vk-$(ARCH)/ggml/src/ggml-vulkan/.vogue-shaders

.PHONY: llama-vk-prepare kmscube-build \
	llama-cpu-build llama-cpu-run llama-cpu-server-build llama-cpu-server-run \
	llama-vk-build llama-vk-run llama-vk-server-build llama-vk-server-run

$(LLAMA_VK_SHADER_STAMP):
	@test -d "$(LLAMA_ROOT)" || { echo "LLAMA_ROOT=$(LLAMA_ROOT) is not a directory"; exit 2; }
	@test -n "$(VULKAN_HEADERS_INCLUDE)" || { echo "VULKAN_HEADERS_INCLUDE is not set"; exit 2; }
	cd $(LLAMA_ROOT) && ARCH=$(ARCH) VOGUE_MARCH=$(VOGUE_MARCH) cmake -S . -B build-unikraft-vk-$(ARCH) \
	    -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/unikraft-clang.cmake \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TOOLS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
	    -DLLAMA_BUILD_SERVER=OFF -DGGML_VULKAN=ON \
	    -DVulkan_INCLUDE_DIR=$(VULKAN_HEADERS_INCLUDE) -DVulkan_LIBRARY=$(VK_LIB)
	cd $(LLAMA_ROOT) && cmake --build build-unikraft-vk-$(ARCH) \
	    --target ggml-vulkan -j$(LLAMA_BUILD_JOBS)
	@touch $@

llama-vk-prepare: $(LLAMA_VK_SHADER_STAMP)

kmscube-build:
	mkdir -p results/kmscube_vgpu_gl/run
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile Kraftfile .

llama-cpu-build:
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile kraft/Kraftfile.llama-cpu .

llama-cpu-run: llama-cpu-build
	python3 scripts/llama_cpu.py --arch "$(ARCH)" --mode bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

llama-cpu-server-build:
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile kraft/Kraftfile.llama-cpu-server .

llama-cpu-server-run: llama-cpu-server-build
	python3 scripts/llama_cpu.py --arch "$(ARCH)" --mode server --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

llama-vk-build: llama-vk-prepare
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile kraft/Kraftfile.llama-vk .

llama-vk-run: llama-vk-build
	python3 scripts/llama_vk.py --arch "$(ARCH)" --mode bench --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

llama-vk-server-build: llama-vk-prepare
	COMPILER=clang $(KRAFT) build --no-prompt --log-type basic --no-update \
		--target $(KRAFT_TARGET) --kraftfile kraft/Kraftfile.llama-vk-server .

llama-vk-server-run: llama-vk-server-build
	python3 scripts/llama_vk.py --arch "$(ARCH)" --mode server --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"
