.PHONY: venus-probe-2d venus-probe-ring venus-check vulkan-check \
	linux-guest-vk-baseline

venus-probe-2d:
	python3 scripts/venus_probe.py --mode 2d --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

venus-probe-ring:
	python3 scripts/venus_probe.py --mode venus-ring --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

venus-check: test-venus venus-probe-2d venus-probe-ring

vulkan-check: vulkan-tests test-dispatch
	python3 scripts/vulkan_check.py --timeout "$(RUN_TIMEOUT)"

linux-guest-vk-baseline:
	python3 scripts/linux_vulkan_baseline.py --model "$(MODEL)" --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"
