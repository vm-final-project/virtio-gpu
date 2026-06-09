/* Shim: include the upstream arm64 quants kernel through the LLAMA_ROOT-relative
 * include path provided by APPLLAMA_CPU_CINCLUDES-y in Makefile.uk. */
#include <ggml-cpu/arch/arm/quants.c>
