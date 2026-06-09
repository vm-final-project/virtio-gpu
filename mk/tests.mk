.PHONY: test-fast test-native test-qemu test-gpu \
	native-tests test-core test-compat test-venus test-dispatch proto-abi vulkan-tests

test-fast: native-tests proto-abi

# Compatibility alias. Keep one canonical dependency graph and no duplicate recipe.
test-native: test-fast

test-qemu: venus-check llm-server-vk-check

test-gpu: vulkan-tests vulkan-check test-dispatch

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
