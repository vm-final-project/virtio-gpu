# CMake toolchain file for building llama.cpp with the Unikraft-compatible
# clang build. Produces libllama.a / libggml*.a static archives.
#
# Path inputs come from the environment so the toolchain file carries no
# personal-checkout paths. Defaults are documented in
# config/deps.json:
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

# macOS Cross-Compilation Setup
if(APPLE)
  if("${_VOGUE_ARCH}" STREQUAL "arm64")
    set(_VOGUE_TARGET "aarch64-linux-gnu")
    set(_VOGUE_MUSL_ARCH "aarch64")
  else()
    set(_VOGUE_TARGET "x86_64-linux-gnu")
    set(_VOGUE_MUSL_ARCH "x86_64")
  endif()

  set(_VOGUE_CROSS_FLAGS "-target ${_VOGUE_TARGET}")

  # Get clang's built-in header directory
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE _CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  # Locate Unikraft musl and libcxx origin directories
  file(GLOB _LIBCXX_ORIGIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/build/libcxx/origin/libcxx-*")
  file(GLOB _LIBMUSL_ORIGIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/build/libmusl/origin/musl-*")

  set(_VOGUE_SYS_INCLUDES
    "${_CLANG_RESOURCE_DIR}/include"
    "${_LIBCXX_ORIGIN_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/libs/libcxx/include"
    "${CMAKE_CURRENT_LIST_DIR}/../.deps/src/unikraft/include"
    "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/build/include"
    "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/build/libsyscall_shim/include"
    "${CMAKE_CURRENT_LIST_DIR}/../.deps/src/unikraft/lib/syscall_shim/include"
    "${_LIBMUSL_ORIGIN_DIR}/include"
    "${_LIBMUSL_ORIGIN_DIR}/arch/${_VOGUE_MUSL_ARCH}"
    "${_LIBMUSL_ORIGIN_DIR}/arch/generic"
    "${CMAKE_CURRENT_LIST_DIR}/../.unikraft/libs/musl/include"
  )

  set(_VOGUE_C_CROSS_FLAGS "${_VOGUE_CROSS_FLAGS} -nostdinc -D__DEFINED_max_align_t")
  foreach(_inc IN LISTS _VOGUE_SYS_INCLUDES)
    if(EXISTS "${_inc}")
      string(APPEND _VOGUE_C_CROSS_FLAGS " -isystem ${_inc}")
    endif()
  endforeach()

  set(_VOGUE_CXX_CROSS_FLAGS "${_VOGUE_CROSS_FLAGS} -nostdinc++ -nostdinc -D__DEFINED_max_align_t")
  foreach(_inc IN LISTS _VOGUE_SYS_INCLUDES)
    if(EXISTS "${_inc}")
      string(APPEND _VOGUE_CXX_CROSS_FLAGS " -isystem ${_inc}")
    endif()
  endforeach()
endif()

# Optional host include/lib roots; the Makefile resolves defaults from
# config/deps.json. Leave empty to fall back on the toolchain's
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

set(_VOGUE_MTUNE "$ENV{VOGUE_MTUNE}")
if("${_VOGUE_MTUNE}" STREQUAL "")
  set(_VOGUE_MTUNE "${_VOGUE_MARCH}")
  if("${_VOGUE_ARCH}" STREQUAL "arm64")
    set(_VOGUE_MTUNE "generic")
  endif()
endif()

set(_VOGUE_CXX_FLAGS "-fPIC -std=c++17 -march=${_VOGUE_MARCH} -mtune=${_VOGUE_MTUNE}")
if(APPLE)
  set(_VOGUE_CXX_FLAGS "${_VOGUE_CXX_CROSS_FLAGS} ${_VOGUE_CXX_FLAGS}")
endif()
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

if(APPLE)
  set(CMAKE_C_FLAGS           "${_VOGUE_C_CROSS_FLAGS} -fPIC -march=${_VOGUE_MARCH} -mtune=${_VOGUE_MTUNE}" CACHE STRING "" FORCE)
else()
  set(CMAKE_C_FLAGS           "-fPIC -march=${_VOGUE_MARCH} -mtune=${_VOGUE_MTUNE}" CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS           "${_VOGUE_CXX_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS    "${_VOGUE_LD_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "${_VOGUE_LD_FLAGS}" CACHE STRING "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
