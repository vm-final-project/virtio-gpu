#!/usr/bin/env python3
"""End-to-end static/runtime gate for llama.cpp server appliances.

Asserts the Unikraft single-application contract:
  * CPU and Vulkan server images boot directly into one app entrypoint.
  * Native server entrypoints do not use Linux-style fork/exec supervision.
  * ELF Loader remains a documented discovery/prototype path, not the release
    architecture.
  * The Vulkan server still pins the plan-optimize.md L4.1/L4.2 QEMU flags and
    surfaces L2.2/L2.3/L3.4 evidence hooks.

Writes results/llama/server_vk_check_latest.{json,md}. Hosts without the
appliance image still pass static checks; missing same-run telemetry becomes a
structured blocker row.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "llama"

VK_KRAFTFILE    = ROOT / "kraft" / "Kraftfile.llama-upstream-vk-server"
CPU_KRAFTFILE   = ROOT / "kraft" / "Kraftfile.llama-upstream-server"
RUNSH           = ROOT / "scripts" / "run_llama_upstream_vk_server.sh"
VK_SERVER_CPP   = ROOT / "apps" / "app-llama-upstream-vk" / "server.cpp"
CPU_SERVER_CPP  = ROOT / "apps" / "app-llama-upstream" / "server.cpp"
VK_CONFIG_UK    = ROOT / "apps" / "app-llama-upstream-vk" / "Config.uk"
CPU_CONFIG_UK   = ROOT / "apps" / "app-llama-upstream" / "Config.uk"
ENV_MATRIX      = ROOT / "config" / "llama_env_matrix.json"
PORTING_PLAN    = ROOT / "docs" / "llama-cpp-unikraft-porting-plan.md"
LLAMA_ROOT      = ROOT.parent / "llama.cpp"
LLAMA_SERVER_MAIN = LLAMA_ROOT / "tools" / "server" / "main.cpp"
LLAMA_SERVER_IMPL = LLAMA_ROOT / "tools" / "server" / "server.cpp"
SERIAL_LOG      = ROOT / "results" / "llama" / "upstream_server_vk_serial.log"

HOSTMEM_RE = re.compile(r"hostmem=(\S+).*blob=true.*venus=true")
RUNSH_FLAGS = ("egl-headless", "blob=true", "venus=true", "hostmem=")
VK_SERVER_FLAGS = (
    "CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE",
    "uk_ggml_vulkan_dispatch_get_info",
    "UK_GGML_VK_DISPATCH_BATCH",
    "llama_server(",
    "--host",
    "0.0.0.0",
    "--port",
    "8080",
    "mode=single-app",
    "no_fork_exec=1",
)
CPU_SERVER_FLAGS = (
    "CONFIG_APP_LLAMA_UPSTREAM_PARALLEL",
    "CONFIG_APP_LLAMA_UPSTREAM_PROMPT_CACHE",
    "mode=single-app",
    "no_fork_exec=1",
)
FORBIDDEN_PROCESS_CALLS = ("fork(", "execv(", "execve(", "posix_spawn(", "system(", "popen(")
READY_LINE_RE = re.compile(
    r"uk-llama-upstream-vk-server: READY .* slots=(?P<slots>\d+) "
    r"ctx_per_slot=(?P<ctx>\d+) batch_size=(?P<batch>\d+) "
    r"ubatch_size=(?P<ubatch>\d+) prompt_cache=(?P<pc>\d) "
    r"batch_enabled=(?P<be>\d) "
    r"hostmem_fixed=(?P<hf>\d)"
)


def _check_static() -> dict:
    findings: list[str] = []
    vk_kraft = VK_KRAFTFILE.read_text() if VK_KRAFTFILE.exists() else ""
    cpu_kraft = CPU_KRAFTFILE.read_text() if CPU_KRAFTFILE.exists() else ""
    runsh = RUNSH.read_text() if RUNSH.exists() else ""
    vk_server = VK_SERVER_CPP.read_text() if VK_SERVER_CPP.exists() else ""
    cpu_server = CPU_SERVER_CPP.read_text() if CPU_SERVER_CPP.exists() else ""
    vk_config = VK_CONFIG_UK.read_text() if VK_CONFIG_UK.exists() else ""
    cpu_config = CPU_CONFIG_UK.read_text() if CPU_CONFIG_UK.exists() else ""
    env_matrix = ENV_MATRIX.read_text() if ENV_MATRIX.exists() else ""
    porting_plan = PORTING_PLAN.read_text() if PORTING_PLAN.exists() else ""
    llama_server_main = LLAMA_SERVER_MAIN.read_text() if LLAMA_SERVER_MAIN.exists() else ""
    llama_server_impl = LLAMA_SERVER_IMPL.read_text() if LLAMA_SERVER_IMPL.exists() else ""

    if "int llama_server(int argc, char ** argv)" not in llama_server_main or "return llama_server(argc, argv);" not in llama_server_main:
        findings.append("upstream-llama: tools/server/main.cpp must delegate executable main() to llama_server(argc, argv)")
    if "int llama_server(int argc, char ** argv)" not in llama_server_impl:
        findings.append("upstream-llama: tools/server/server.cpp must expose reusable llama_server(argc, argv)")

    if not HOSTMEM_RE.search(vk_kraft):
        findings.append("L4.1: kraft/Kraftfile.llama-upstream-vk-server missing hostmem=…,blob=true,venus=true")
    if RUNSH.exists() and not all(flag in runsh for flag in RUNSH_FLAGS):
        findings.append("L4.2: scripts/run_llama_upstream_vk_server.sh missing one of "
                        + ", ".join(RUNSH_FLAGS))
    for flag in VK_SERVER_FLAGS:
        if flag not in vk_server:
            findings.append(f"single-app/vk: server.cpp missing {flag}")
    for flag in CPU_SERVER_FLAGS:
        if flag not in cpu_server:
            findings.append(f"single-app/cpu: server.cpp missing {flag}")
    if "APP_LLAMA_UPSTREAM_VK_PARALLEL" not in vk_config:
        findings.append("L2.2: Vulkan Config.uk missing APP_LLAMA_UPSTREAM_VK_PARALLEL")
    if "APP_LLAMA_UPSTREAM_VK_BATCH" not in vk_config:
        findings.append("L2.2: Vulkan Config.uk missing APP_LLAMA_UPSTREAM_VK_BATCH")
    if "APP_LLAMA_UPSTREAM_VK_UBATCH" not in vk_config:
        findings.append("L2.2: Vulkan Config.uk missing APP_LLAMA_UPSTREAM_VK_UBATCH")
    if "APP_LLAMA_UPSTREAM_PARALLEL" not in cpu_config:
        findings.append("L2.2: CPU Config.uk missing APP_LLAMA_UPSTREAM_PARALLEL")

    for label, text in (("cpu-server.cpp", cpu_server), ("vk-server.cpp", vk_server)):
        for needle in FORBIDDEN_PROCESS_CALLS:
            if needle in text:
                findings.append(f"single-app/{label}: forbidden Linux supervisor call {needle}")

    for required in ("qemu-unikraft-cpu-server", "qemu-unikraft-vulkan-server",
                     "qemu-unikraft-llama-server-only",
                     "qemu-unikraft-llama-vulkan-server-only"):
        if required not in env_matrix:
            findings.append(f"env-matrix: missing {required}")

    if not all(term in porting_plan for term in ("ELF Loader", "discovery", "native", "fork", "exec")):
        findings.append("porting-plan: must document ELF Loader as discovery-only and native no-fork/no-exec path")
    if "CONFIG_APP_LLAMA_UPSTREAM_MODE_SERVER" not in cpu_kraft:
        findings.append("cpu-kraftfile: missing server-mode Kconfig selection")

    return {"static_findings": findings}


def _check_runtime() -> dict:
    if not SERIAL_LOG.exists():
        return {"runtime_status": "blocked:no-serial-log",
                "runtime_log":    str(SERIAL_LOG.relative_to(ROOT))}
    text = SERIAL_LOG.read_text(errors="replace")
    m = READY_LINE_RE.search(text)
    if not m:
        return {"runtime_status": "blocked:no-ready-line",
                "runtime_log":    str(SERIAL_LOG.relative_to(ROOT))}
    return {
        "runtime_status": "pass",
        "runtime_log":    str(SERIAL_LOG.relative_to(ROOT)),
        "slots":          int(m.group("slots")),
        "ctx_per_slot":   int(m.group("ctx")),
        "batch_size":     int(m.group("batch")),
        "ubatch_size":    int(m.group("ubatch")),
        "prompt_cache":   bool(int(m.group("pc"))),
        "dispatch_batch_enabled": bool(int(m.group("be"))),
        "hostmem_fixed":  bool(int(m.group("hf"))),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="non-zero exit if any static finding is unresolved")
    args = parser.parse_args(argv)

    static = _check_static()
    runtime = _check_runtime()
    generated = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    payload = {
        "metadata": {
            "generated_utc": generated,
            "source":        "scripts/llm_server_vk_check.py",
            "principle":     "plan-optimize.md Phase-2 verification gate",
        },
        **static,
        **runtime,
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "server_vk_check_latest.json").write_text(json.dumps(payload, indent=2) + "\n")
    md = ["# llm.server.vk Phase-2 gate", "",
          f"Generated: `{generated}`", "",
          "## Static",
          ""]
    if static["static_findings"]:
        md.append("FAIL:")
        for f in static["static_findings"]:
            md.append(f"- {f}")
    else:
        md.append("PASS — CPU/Vulkan server artifacts carry the direct single-application contract; ../llama.cpp exposes llama_server(argc, argv); no forbidden fork/exec-style supervisor calls were found in app server entrypoints.")
    md.extend(["", "## Runtime", "",
               f"status = `{runtime['runtime_status']}` log = `{runtime.get('runtime_log','n/a')}`"])
    (OUT / "server_vk_check_latest.md").write_text("\n".join(md) + "\n")

    for f in static["static_findings"]:
        print(f"llm-server-vk: STATIC FAIL {f}")
    print(f"llm-server-vk: runtime={runtime['runtime_status']}")
    if args.check and static["static_findings"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
