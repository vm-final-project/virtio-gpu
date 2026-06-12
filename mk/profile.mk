# Phase 1/2 profiling targets (docs/plan-profile.md). Each wraps the canonical
# llama-vk bench QEMU invocation in one host-side profiling tool and writes a
# results/profile/*.json artifact (blocked:* on hosts without perf/KVM/Venus).

PROFILE_TIMEOUT ?= 600
PROFILE_FREQ    ?= 999

.PHONY: profile-vk-kvm-stat profile-vk-host-record profile-vk-guest-record \
	profile-vk-qemu-trace profile-vk-all profile-vk-bench-perf-logger

profile-vk-kvm-stat: llama-vk-bench-build
	python3 scripts/profile-llama-vk.py --tool kvm-stat --arch "$(ARCH)" \
		--model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(PROFILE_TIMEOUT)"

profile-vk-host-record: llama-vk-bench-build
	python3 scripts/profile-llama-vk.py --tool host-record --arch "$(ARCH)" \
		--model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(PROFILE_TIMEOUT)" \
		--freq "$(PROFILE_FREQ)"

profile-vk-guest-record: llama-vk-bench-build
	python3 scripts/profile-llama-vk.py --tool guest-record --arch "$(ARCH)" \
		--model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(PROFILE_TIMEOUT)" \
		--freq "$(PROFILE_FREQ)"

profile-vk-qemu-trace: llama-vk-bench-build
	python3 scripts/profile-llama-vk.py --tool qemu-trace --arch "$(ARCH)" \
		--model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(PROFILE_TIMEOUT)"

profile-vk-all: profile-vk-kvm-stat profile-vk-host-record \
	profile-vk-guest-record profile-vk-qemu-trace

# Phase 2: in-guest ggml per-op GPU timings (GGML_VK_PERF_LOGGER).
profile-vk-bench-perf-logger: llama-vk-bench-build
	python3 scripts/app-llama-vk.py --arch "$(ARCH)" --mode bench \
		--model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(PROFILE_TIMEOUT)" \
		--perf-logger
