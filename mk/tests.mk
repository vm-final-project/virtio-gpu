.PHONY: test-fast test-native test-qemu test-gpu \
	native-tests test-venus proto-abi

test-fast: native-tests proto-abi

# Compatibility alias. Keep one canonical dependency graph and no duplicate recipe.
test-native: test-fast

test-qemu: venus-check

test-gpu: vulkan-check

native-tests:
	$(MAKE) -C tests native

test-venus:
	$(MAKE) -C tests test-venus

proto-abi:
	$(MAKE) -C tests proto-abi
