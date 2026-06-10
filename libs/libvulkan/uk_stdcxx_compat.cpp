// Libstdc++ ABI compatibility shims for prebuilt ggml archives.
// libggml.a / libggml-base.a / libggml-cpu.a were compiled against GCC libstdc++
// and reference a handful of __throw_* helpers that don't exist in Unikraft's libc++.
// These weak stubs satisfy the undefined references without ever being called at runtime
// (ggml error paths trap the kernel anyway).

#include <cstdlib>
#include <exception>

// GCC's C++ exception machinery emits calls to __cxa_call_terminate when an
// exception escapes a noexcept boundary; GCC ships it in libsupc++, but this
// image links LLVM libc++abi, which lacks it. Provide a GCC-compatible helper
// (adopt the in-flight exception, then terminate). Weak so a real libsupc++
// wins if ever linked.
extern "C" void *__cxa_begin_catch(void *) noexcept;

extern "C" __attribute__((weak)) void __cxa_call_terminate(void *ue_header) {
    if (ue_header)
        __cxa_begin_catch(ue_header);
    std::terminate();
}

namespace std {

__attribute__((weak, noreturn)) void __throw_length_error(char const*) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_bad_alloc() {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_logic_error(char const*) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_system_error(int) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_out_of_range(char const*) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_out_of_range_fmt(char const*, ...) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_runtime_error(char const*) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_invalid_argument(char const*) {
    __builtin_trap();
}

__attribute__((weak, noreturn)) void __throw_overflow_error(char const*) {
    __builtin_trap();
}

} // namespace std
