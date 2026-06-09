.PHONY: venus-probe-2d venus-probe-ring venus-perf venus-check \
	vulkan-check eval eval-check current-stage-check \
	llm-server-vk-check llm-server-vk-throughput-check linux-guest-vk-baseline

venus-probe-2d:
	python3 -m scripts.vogue probe venus --mode 2d --allow-blocked

venus-probe-ring:
	python3 -m scripts.vogue probe venus --mode venus-ring --allow-blocked

venus-perf: venus-probe-2d venus-probe-ring
	python3 -m scripts.vogue evaluate venus --repetitions 5 --allow-blocked

venus-check: venus-perf

vulkan-check:
	python3 -m scripts.vogue evaluate vulkan --repetitions 3 --allow-blocked

eval:
	python3 -m scripts.vogue evaluate matrix --check

eval-check: venus-check vulkan-check
	python3 -m scripts.vogue evaluate matrix --check

current-stage-check: eval-check
	python3 -m scripts.vogue report stage --check

llm-server-vk-check: llama-vk-server-run
	python3 -m scripts.vogue evaluate server-vk --check

llm-server-vk-throughput-check: llm-server-vk-check
	python3 -m scripts.vogue evaluate server-vk-throughput --check

linux-guest-vk-baseline:
	python3 -m scripts.vogue capture linux-guest-vk
