#pragma once
#include <stdint.h>
#define DRM_FORMAT_XRGB8888 0x34325258u
#define DRM_FORMAT_ARGB8888 0x34325241u
#define DRM_FORMAT_NV12     0x3231564eu
#define DRM_FORMAT_MOD_LINEAR 0ULL
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID (((uint64_t)0) | ((1ULL << 56) - 1))
#endif
