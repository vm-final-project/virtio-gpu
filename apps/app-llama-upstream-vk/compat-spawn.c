/* SPDX-License-Identifier: MIT */
/*
 * compat-spawn.c — Unikraft link shim for posix_spawnp.
 *
 * Upstream llama.cpp common/ (subprocess helper used by the HuggingFace model
 * downloader) references posix_spawnp, which Unikraft's posix-process does not
 * provide. The single-application server appliance never downloads or spawns:
 * its GGUF is delivered over 9pfs and it boots directly into one entrypoint
 * (no fork/exec launcher — that is part of the single-app contract). This stub
 * lets the image link; if the unreachable download path were ever taken it
 * fails cleanly with ENOSYS rather than silently spawning.
 */
#include <errno.h>
#include <spawn.h>
#include <sys/types.h>

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[])
{
	(void)pid; (void)file; (void)file_actions; (void)attrp;
	(void)argv; (void)envp;
	return ENOSYS; /* posix_spawn* returns the error number directly */
}
