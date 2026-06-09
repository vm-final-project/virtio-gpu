/* Shim: include the upstream x86 quants kernel through the LLAMA_ROOT-relative
 * include path so the project tree contains no personal-checkout paths. The
 * search root is provided by APPLLAMA_CPU_CINCLUDES-y in Makefile.uk
 * (`-I$(LLAMA_ROOT)/ggml/src`). */
#include <ggml-cpu/arch/x86/quants.c>
