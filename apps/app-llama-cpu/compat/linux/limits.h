/* SPDX-License-Identifier: MIT */
/*
 * compat/linux/limits.h — Unikraft/musl build shim.
 *
 * Upstream llama.cpp common/{arg,download}.cpp include <linux/limits.h> for
 * PATH_MAX/NAME_MAX. Unikraft+musl expose those through <limits.h> and have no
 * Linux UAPI tree, so this shim forwards to <limits.h> and backfills the two
 * macros if the libc happens not to define them. Only on the app include path
 * for the llama-server appliance; the Linux host build is unaffected.
 */
#ifndef _VOGUE_COMPAT_LINUX_LIMITS_H
#define _VOGUE_COMPAT_LINUX_LIMITS_H

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#endif /* _VOGUE_COMPAT_LINUX_LIMITS_H */
