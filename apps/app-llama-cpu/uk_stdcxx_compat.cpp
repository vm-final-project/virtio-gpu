// GCC <-> LLVM C++ runtime ABI compatibility shim for the in-tree GCC build.
//
// The ggml/llama and libc++ translation units are compiled with the GCC
// cross-toolchain, whose exception machinery emits calls to GCC-libsupc++
// helpers. This image links LLVM libc++abi instead, which does not define them.
// Provide GCC-compatible versions so the final link resolves. They are declared
// weak so a real libsupc++ (if ever linked) takes precedence.

#include <exception>

extern "C" void *__cxa_begin_catch(void *) noexcept;

// Emitted by GCC when an exception escapes a noexcept boundary. GCC's libsupc++
// adopts the in-flight exception (so the unwinder bookkeeping is consistent),
// then terminates. Mirror that behaviour.
extern "C" __attribute__((weak)) void __cxa_call_terminate(void *ue_header) {
	if (ue_header)
		__cxa_begin_catch(ue_header);
	std::terminate();
}
