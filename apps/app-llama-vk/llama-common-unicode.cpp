/* Unique-basename shim for upstream common/unicode.cpp.
 * Required because upstream src/unicode.cpp is also built into this app and
 * Unikraft keys translation units by basename. */
#include "../../../llama.cpp/common/unicode.cpp"
