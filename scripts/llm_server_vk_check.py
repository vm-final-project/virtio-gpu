#!/usr/bin/env python3
"""End-to-end static/runtime gate for llama.cpp server appliances."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from artifact_utils import blocked_artifact, make_artifact, write_json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "llama"
SOURCE = "scripts/llm_server_vk_check.py"

VK_KRAFTFILE = ROOT / "kraft" / "Kraftfile.llama-upstream-vk-server"
CPU_KRAFTFILE = ROOT / "kraft" / "Kraftfile.llama-upstream-server"
VK_SERVER_CPP = ROOT / "apps" / "app-llama-upstream-vk" / "server.cpp"
CPU_SERVER_CPP = ROOT / "apps" / "app-llama-upstream" / "server.cpp"
VK_CONFIG_UK = ROOT / "apps" / "app-llama-upstream-vk" / "Config.uk"
CPU_CONFIG_UK = ROOT / "apps" / "app-llama-upstream" / "Config.uk"
ENV_MATRIX = ROOT / "config" / "llama_env_matrix.json"
PORTING_PLAN = ROOT / "docs" / "llama-cpp-unikraft-porting-plan.md"
LLAMA_ROOT = ROOT.parent / "llama.cpp"
LLAMA_SERVER_MAIN = LLAMA_ROOT / "tools" / "server" / "main.cpp"
LLAMA_SERVER_IMPL = LLAMA_ROOT / "tools" / "server" / "server.cpp"
SERIAL_LOG = ROOT / "results" / "llama" / "upstream_server_vk_serial.log"
SERVER_VK_RUNTIME = ROOT / "results" / "llama" / "upstream_server_vk.json"

HOSTMEM_RE = re.compile(r"hostmem=(\S+).*blob=true.*venus=true")
VK_NET_KCONFIG = (
    "CONFIG_LIBVIRTIO_NET",
    "CONFIG_LIBUKNETDEV",
    "CONFIG_LIBUKNETDEV_EINFO_LIBPARAM",
    "CONFIG_LIBLWIP",
    "CONFIG_LWIP_TCP",
    "CONFIG_LWIP_SOCKET",
    "CONFIG_LWIP_DHCP",
    "CONFIG_LIBUKRANDOM_DEVFS",
)
VK_SERVER_FLAGS = (
    "CONFIG_APP_LLAMA_UPSTREAM_VK_PARALLEL",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_BATCH",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_UBATCH",
    "CONFIG_APP_LLAMA_UPSTREAM_VK_PROMPT_CACHE",
    "uk_vulkan_get_info",
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
    r"(?:ring_enabled=\d+ )?"
    r"hostmem_fixed=(?P<hf>\d)"
)


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def check_static() -> list[str]:
    findings: list[str] = []
    vk_kraft = read(VK_KRAFTFILE)
    cpu_kraft = read(CPU_KRAFTFILE)
    vk_server = read(VK_SERVER_CPP)
    cpu_server = read(CPU_SERVER_CPP)
    vk_config = read(VK_CONFIG_UK)
    cpu_config = read(CPU_CONFIG_UK)
    env_matrix = read(ENV_MATRIX)
    porting_plan = read(PORTING_PLAN)
    llama_server_main = read(LLAMA_SERVER_MAIN)
    llama_server_impl = read(LLAMA_SERVER_IMPL)

    if "int llama_server(int argc, char ** argv)" not in llama_server_main or "return llama_server(argc, argv);" not in llama_server_main:
        findings.append("upstream-llama: tools/server/main.cpp must delegate executable main() to llama_server(argc, argv)")
    if "int llama_server(int argc, char ** argv)" not in llama_server_impl:
        findings.append("upstream-llama: tools/server/server.cpp must expose reusable llama_server(argc, argv)")
    if not HOSTMEM_RE.search(vk_kraft):
        findings.append("L4.1: kraft/Kraftfile.llama-upstream-vk-server missing hostmem=…,blob=true,venus=true")
    for sym in VK_NET_KCONFIG:
        if sym not in vk_kraft:
            findings.append(f"http: kraft/Kraftfile.llama-upstream-vk-server missing {sym} (lwIP/netdev HTTP path)")
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
                     "qemu-unikraft-llama-server-only", "qemu-unikraft-llama-vulkan-server-only"):
        if required not in env_matrix:
            findings.append(f"env-matrix: missing {required}")
    if not all(term in porting_plan for term in ("ELF Loader", "discovery", "native", "fork", "exec")):
        findings.append("porting-plan: must document ELF Loader as discovery-only and native no-fork/no-exec path")
    if "CONFIG_APP_LLAMA_UPSTREAM_MODE_SERVER" not in cpu_kraft:
        findings.append("cpu-kraftfile: missing server-mode Kconfig selection")
    return findings


def read_http_proof() -> dict:
    if not SERVER_VK_RUNTIME.exists():
        return {"http_status": "blocked:no-runtime-json"}
    try:
        data = json.loads(SERVER_VK_RUNTIME.read_text())
    except (OSError, json.JSONDecodeError):
        return {"http_status": "blocked:bad-runtime-json"}
    http = data.get("http") or {}
    health = http.get("health_status")
    return {
        "http_status": "pass" if health == 200 else f"blocked:health={health}",
        "http_health": health,
        "http_models_status": http.get("models_status"),
        "http_completion_status": http.get("completion_status"),
        "http_completion_content": http.get("completion_content"),
        "http_endpoint": http.get("endpoint"),
    }


def check_runtime() -> dict:
    if not SERIAL_LOG.exists():
        return {"runtime_status": "blocked:no-serial-log", "runtime_log": str(SERIAL_LOG.relative_to(ROOT))}
    text = SERIAL_LOG.read_text(errors="replace")
    match = READY_LINE_RE.search(text)
    if not match:
        return {"runtime_status": "blocked:no-ready-line", "runtime_log": str(SERIAL_LOG.relative_to(ROOT))}
    out = {
        "runtime_status": "pass",
        "runtime_log": str(SERIAL_LOG.relative_to(ROOT)),
        "slots": int(match.group("slots")),
        "ctx_per_slot": int(match.group("ctx")),
        "batch_size": int(match.group("batch")),
        "ubatch_size": int(match.group("ubatch")),
        "prompt_cache": bool(int(match.group("pc"))),
        "dispatch_batch_enabled": bool(int(match.group("be"))),
        "hostmem_fixed": bool(int(match.group("hf"))),
    }
    out.update(read_http_proof())
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="non-zero exit if any static finding is unresolved")
    args = parser.parse_args(argv)

    static_findings = check_static()
    runtime = check_runtime()
    runtime_status = runtime["runtime_status"]
    http_status = runtime.get("http_status", "blocked:no-http-proof")
    if static_findings:
        status = "fail"
    elif runtime_status.startswith("blocked:"):
        status = runtime_status
    elif http_status.startswith("blocked:"):
        status = http_status
    else:
        status = "pass"

    checks = [
        {"id": "static_contract", "status": "pass" if not static_findings else "fail"},
        {"id": "runtime_ready", "status": runtime_status},
        {"id": "http_probe", "status": http_status},
    ]
    extra = {
        "static_findings": static_findings,
        **runtime,
    }
    if status.startswith("blocked:"):
        payload = blocked_artifact(
            source=SOURCE,
            status=status,
            headline="Single-application Vulkan llama.cpp server contract",
            stage="runtime",
            first_missing_dependency=runtime.get("runtime_log"),
            claim_allowed="Static single-application contract can still pass; runtime serving remains explicitly blocked.",
            claim_forbidden="Passing Vulkan server runtime or HTTP serving claim without READY-line and same-run HTTP proof.",
            next_step="Regenerate the Vulkan server runtime capture and rerun llm_server_vk_check.py.",
            counts={"static_findings": 0},
            artifacts={
                "serial_log": str(SERIAL_LOG.relative_to(ROOT)),
                "runtime_json": str(SERVER_VK_RUNTIME.relative_to(ROOT)),
            },
            checks=checks,
            extra=extra,
        )
    else:
        payload = make_artifact(
            source=SOURCE,
            status=status,
            headline="Single-application Vulkan llama.cpp server contract",
            counts={"static_findings": len(static_findings)},
            artifacts={
                "serial_log": str(SERIAL_LOG.relative_to(ROOT)),
                "runtime_json": str(SERVER_VK_RUNTIME.relative_to(ROOT)),
            },
            checks=checks,
            extra=extra,
        )

    write_json(OUT / "server_vk_check.json", payload)
    for finding in static_findings:
        print(f"llm-server-vk: STATIC FAIL {finding}")
    print(f"llm-server-vk: status={payload['status']} runtime={runtime_status} http={http_status}")
    if args.check and static_findings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
