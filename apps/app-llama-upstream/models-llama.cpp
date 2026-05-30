/* Shim: src/models/llama.cpp shares a basename with src/llama.cpp, so a direct
 * SRCS-y entry would collide. Wrap the include via this shim file with a
 * unique basename and let Makefile.uk supply the search path. */
#include <models/llama.cpp>
