.PHONY: venus-probe-2d venus-probe-ring venus-check vulkan-check

venus-probe-2d:
	python3 scripts/app-vulkan-sample.py --arch "$(ARCH)" --mode 2d --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

venus-probe-ring:
	python3 scripts/app-vulkan-sample.py --arch "$(ARCH)" --mode venus-ring --qemu "$(QEMU)" --timeout "$(RUN_TIMEOUT)"

venus-check: test-venus venus-probe-2d venus-probe-ring

vulkan-check: test-dispatch
