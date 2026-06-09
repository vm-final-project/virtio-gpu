# CMake toolchain file for building llama.cpp with the Unikraft-compatible
# clang build. Produces libllama.a / libggml*.a static archives.
#
# Path inputs come from the environment so the toolchain file carries no
# personal-checkout paths. Defaults are documented in
# config/external_paths.json:
#
#   LLAMA_ROOT              upstream llama.cpp source root
#   VULKAN_HEADERS_INCLUDE  Vulkan-Headers (v1.3.352) include dir
#   SPIRV_HEADERS_INCLUDE   SPIRV-Headers (matched to Vulkan 352) include dir
#   HOST_CXX_INCLUDE        host C++ standard-library include (optional)
#   HOST_GCC_LIB            host GCC runtime library dir (optional)
#
# Usage (from the project Makefile):
#   cmake -S "$LLAMA_ROOT" -B "$LLAMA_ROOT/build-unikraft-vk" \
#         -DCMAKE_TOOLCHAIN_FILE=$(realpath cmake/unikraft-clang.cmake) ...

set(CMAKE_SYSTEM_NAME Linux)
set(_VOGUE_ARCH "$ENV{ARCH}")
if("${_VOGUE_ARCH}" STREQUAL "")
  set(_VOGUE_ARCH "x86_64")
endif()
set(CMAKE_SYSTEM_PROCESSOR ${_VOGUE_ARCH})

set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

# This toolchain only ever emits static archives (libllama.a / libggml*.a);
# the final link happens later inside the Unikraft unikernel against lib-libcxx.
# Tell CMake's compiler probe to build a static library rather than an
# executable so configuration does not require a host C++ link library
# (e.g. -lstdc++), which a freestanding cross host may not provide.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Optional host include/lib roots; the Makefile resolves defaults from
# config/external_paths.json. Leave empty to fall back on the toolchain's
# built-in search paths.
set(_VOGUE_HOST_CXX_INCLUDE  "$ENV{HOST_CXX_INCLUDE}")
set(_VOGUE_HOST_GCC_LIB      "$ENV{HOST_GCC_LIB}")
set(_VOGUE_SPIRV_HEADERS_INC "$ENV{SPIRV_HEADERS_INCLUDE}")
set(_VOGUE_VULKAN_HEADERS_INC "$ENV{VULKAN_HEADERS_INCLUDE}")

# Build for this host's microarchitecture so the static llama/ggml archives use
# the same ISA (AVX2/FMA/F16C/BMI2 on this Broadwell host) the appliance runs on
# under QEMU -cpu host. Override by exporting VOGUE_MARCH for a different target.
set(_VOGUE_MARCH "$ENV{VOGUE_MARCH}")
if("${_VOGUE_MARCH}" STREQUAL "")
  if("${_VOGUE_ARCH}" STREQUAL "arm64")
    set(_VOGUE_MARCH "armv8-a")
  else()
    set(_VOGUE_MARCH "native")
  endif()
endif()

set(_VOGUE_MTUNE "${_VOGUE_MARCH}")
if("${_VOGUE_ARCH}" STREQUAL "arm64")
  set(_VOGUE_MTUNE "generic")
endif()

set(_VOGUE_CXX_FLAGS "-fPIC -std=c++17 -march=${_VOGUE_MARCH} -mtune=${_VOGUE_MTUNE}")
foreach(_inc IN ITEMS
    "${_VOGUE_HOST_CXX_INCLUDE}"
    "${_VOGUE_SPIRV_HEADERS_INC}"
    "${_VOGUE_VULKAN_HEADERS_INC}")
  if(NOT "${_inc}" STREQUAL "")
    string(APPEND _VOGUE_CXX_FLAGS " -isystem ${_inc}")
  endif()
endforeach()

set(_VOGUE_LD_FLAGS "")
if(NOT "${_VOGUE_HOST_GCC_LIB}" STREQUAL "")
  string(APPEND _VOGUE_LD_FLAGS "-L${_VOGUE_HOST_GCC_LIB}")
endif()

set(CMAKE_C_FLAGS             "-fPIC -march=${_VOGUE_MARCH} -mtune=${_VOGUE_MTUNE}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS           "${_VOGUE_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS    "${_VOGUE_LD_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "${_VOGUE_LD_FLAGS}" CACHE STRING "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
