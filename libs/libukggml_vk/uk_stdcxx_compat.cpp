// Libstdc++ ABI compatibility shims for prebuilt ggml archives.
// libggml.a / libggml-base.a / libggml-cpu.a were compiled against GCC libstdc++
// and reference a handful of __throw_* helpers that don't exist in Unikraft's libc++.
// These weak stubs satisfy the undefined references without ever being called at runtime
// (ggml error paths trap the kernel anyway).

#include <cstdlib>

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
