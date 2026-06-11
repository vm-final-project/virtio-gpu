#include "build-info.h"

#include <cstdio>
#include <string>

int llama_build_number(void) { return 0; }
const char * llama_commit(void) { return "unikraft-native"; }
const char * llama_compiler(void) { return "unikraft-clang"; }
const char * llama_build_target(void) { return "unikraft"; }

const char * llama_build_info(void) {
    static std::string s = "b0-unikraft-native";
    return s.c_str();
}

void llama_print_build_info(void) {
    fprintf(stderr, "%s: build = %d (%s)\n", __func__, llama_build_number(), llama_commit());
    fprintf(stderr, "%s: built with %s for %s\n", __func__, llama_compiler(), llama_build_target());
}
