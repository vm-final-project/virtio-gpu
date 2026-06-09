/* Shim: pull in the upstream `ggml-cpu.cpp` with a unique basename so the
 * Unikraft Makefile.uk SRCS-y namespace does not collide with `ggml-cpu.c`. */
#include <ggml-cpu/ggml-cpu.cpp>
