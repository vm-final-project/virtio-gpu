#!/usr/bin/env python3
"""Validate and plan llama.cpp benchmark runs across VOGUE environments.

The matrix is intentionally JSON (config/llama_env_matrix.json) so the fast
artifact checks do not depend on PyYAML.  By default this script performs a
safe dry-run plan; pass --execute to run environments whose prerequisites are
present. Missing QEMU/GPU/SSH/Kraft assets are reported as structured blockers,
not as synthetic performance data.
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "llama_env_matrix.json"
RESULTS = ROOT / "results" / "llama-env"


def load_config(path: Path = CONFIG) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text())
    except FileNotFoundError:
        raise SystemExit(f"missing config: {path.relative_to(ROOT)}")
    except json.JSONDecodeError as exc:
        raise SystemExit(f"invalid JSON in {path.relative_to(ROOT)}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit("llama env matrix root must be an object")
    return data


def bench_args(cfg: dict[str, Any], *, threads: int, model: str, extra: list[str]) -> list[str]:
    lb = cfg.get("llama_bench_args", {})
    args: list[str] = ["-m", model]
    for p in lb.get("prompt_tokens", []):
        args += ["-p", str(p)]
    for n in lb.get("gen_tokens", []):
        args += ["-n", str(n)]
    args += ["-r", str(lb.get("repetitions", 5)), "-o", str(lb.get("output_format", "json"))]
    args += ["--threads", str(threads)]
    args += [str(x) for x in lb.get("extra", [])]
    args += extra
    return args


def validate(cfg: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    envs = cfg.get("environments")
    if not isinstance(envs, list) or not envs:
        return ["environments must be a non-empty list"]
    ids = set()
    families = set()
    for i, env in enumerate(envs):
        if not isinstance(env, dict):
            errors.append(f"environments[{i}] must be an object")
            continue
        env_id = env.get("id")
        if not isinstance(env_id, str) or not env_id:
            errors.append(f"environments[{i}] missing id")
        elif env_id in ids:
            errors.append(f"duplicate environment id: {env_id}")
        else:
            ids.add(env_id)
        family = env.get("family")
        if not isinstance(family, str) or "+" not in family:
            errors.append(f"{env_id}: invalid family {family!r}")
        else:
            families.add(family)
        threads = env.get("threads")
        if not isinstance(threads, list) or not threads or not all(isinstance(t, int) and t > 0 for t in threads):
            errors.append(f"{env_id}: threads must be a non-empty list of positive integers")
        if env.get("mode") not in {"bench", "server"}:
            errors.append(f"{env_id}: mode must be bench or server")
        if not env.get("claim_allowed") or not env.get("claim_forbidden"):
            errors.append(f"{env_id}: missing claim boundaries")
        if env.get("runner") == "qemu-unikraft":
            for key in ("kraftfile", "image", "single_app"):
                if not env.get(key):
                    errors.append(f"{env_id}: qemu-unikraft runner missing {key}")
    for required in cfg.get("required_families", []):
        if required not in families:
            errors.append(f"missing required family: {required}")
    appliance_ids = {a.get("id") for a in cfg.get("appliances", []) if isinstance(a, dict)}
    for required in (
        "qemu-unikraft-llama-bench-only",
        "qemu-unikraft-llama-server-only",
        "qemu-unikraft-llama-vulkan-bench-only",
        "qemu-unikraft-llama-vulkan-server-only",
    ):
        if required not in appliance_ids:
            errors.append(f"missing required appliance: {required}")
    return errors


def select_envs(cfg: dict[str, Any], ids: list[str], mode: str) -> list[dict[str, Any]]:
    envs = cfg["environments"]
    if ids:
        wanted = set(ids)
        envs = [e for e in envs if e.get("id") in wanted]
        missing = wanted - {e.get("id") for e in envs}
        if missing:
            raise SystemExit(f"unknown env id(s): {', '.join(sorted(missing))}")
    if mode != "all":
        envs = [e for e in envs if e.get("mode") == mode]
    return envs


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(str(p)) for p in parts)


def plan_for_env(cfg: dict[str, Any], env: dict[str, Any], args: argparse.Namespace) -> list[dict[str, Any]]:
    model = args.model or str((ROOT / cfg.get("model", {}).get("default_path", "")).resolve())
    thread_values = [args.threads] if args.threads else env["threads"]
    out: list[dict[str, Any]] = []
    for threads in thread_values:
        extra = [str(x) for x in env.get("extra_args", [])] + list(args.extra_arg or [])
        common = bench_args(cfg, threads=threads, model=model, extra=extra)
        runner = env.get("runner", "local")
        env_vars: dict[str, str] = {}
        command: list[str]
        if runner == "ssh":
            ssh = os.environ.get(str(env.get("ssh_env", "QEMU_LINUX_SSH")), "<set QEMU_LINUX_SSH>")
            remote = [str(env.get("remote_binary")), "-m", str(env.get("remote_model"))] + common[2:]
            command = ["ssh", ssh, shell_join(remote)]
        elif runner == "qemu-unikraft":
            command = [os.environ.get("QEMU", "qemu-system-x86_64"), "-kernel", str(ROOT / env["image"])] + [str(x) for x in env.get("qemu_args", [])]
            command += ["-append", f"model={env.get('virtfs_tag', 'model')} threads={threads} mode={env.get('mode')}"]
        else:
            binary = os.environ.get(str(env.get("binary_env", "")), str(env.get("binary_default", "llama-bench")))
            if env.get("device_env"):
                env_vars[str(env["device_env"])] = str(os.environ.get(str(env["device_env"]), env.get("device_default", "")))
            command = [binary] + common
        out.append({
            "env_id": env["id"],
            "family": env["family"],
            "label": env["label"],
            "backend": env.get("backend"),
            "mode": env.get("mode"),
            "threads": threads,
            "runner": runner,
            "single_app": env.get("single_app"),
            "kraftfile": env.get("kraftfile"),
            "command": command,
            "command_string": shell_join(command),
            "env": env_vars,
            "claim_allowed": env.get("claim_allowed"),
            "claim_forbidden": env.get("claim_forbidden"),
            "status": "planned"
        })
    return out


def prerequisite_status(item: dict[str, Any]) -> tuple[str, str]:
    runner = item["runner"]
    cmd0 = item["command"][0]
    if runner == "local":
        binary = Path(cmd0)
        found = binary.exists() if binary.is_absolute() or binary.parent != Path(".") else shutil.which(cmd0) is not None
        return ("ready", "") if found else ("blocked:binary-missing", cmd0)
    if runner == "ssh":
        return ("blocked:ssh-target-not-configured", "set QEMU_LINUX_SSH") if "<set" in item["command"][1] else ("ready", "")
    if runner == "qemu-unikraft":
        kernel = Path(item["command"][2])
        if not kernel.exists():
            return "blocked:unikraft-image-missing", str(kernel.relative_to(ROOT) if kernel.is_relative_to(ROOT) else kernel)
        return "ready", ""
    return "blocked:unknown-runner", runner


def execute_item(item: dict[str, Any], timeout: int) -> dict[str, Any]:
    status, reason = prerequisite_status(item)
    if status != "ready":
        item.update({"status": status, "blocker": reason})
        return item
    try:
        proc = subprocess.run(item["command"], cwd=ROOT, env={**os.environ, **item.get("env", {})},
                              capture_output=True, text=True, timeout=timeout)
        item.update({
            "status": "pass" if proc.returncode == 0 else "fail",
            "returncode": proc.returncode,
            "stdout_tail": proc.stdout[-2000:],
            "stderr_tail": proc.stderr[-2000:]
        })
    except subprocess.TimeoutExpired:
        item.update({"status": "blocked:timeout", "timeout_s": timeout})
    return item


def write_output(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--config", type=Path, default=CONFIG)
    p.add_argument("--check", action="store_true", help="validate matrix and exit")
    p.add_argument("--list", action="store_true", help="list configured environments")
    p.add_argument("--dry-run", action="store_true", help="write a run plan without executing")
    p.add_argument("--execute", action="store_true", help="execute selected ready environments")
    p.add_argument("--env", action="append", default=[], help="environment id to select; repeatable")
    p.add_argument("--mode", choices=["bench", "server", "all"], default="bench")
    p.add_argument("--threads", type=int, help="override thread count for selected envs")
    p.add_argument("--model", help="override model path")
    p.add_argument("--extra-arg", action="append", default=[], help="extra llama-bench arg; repeatable")
    p.add_argument("--timeout", type=int, default=300)
    p.add_argument("--output", type=Path, default=RESULTS / "latest-plan.json")
    args = p.parse_args(argv)

    cfg = load_config(args.config)
    errors = validate(cfg)
    if errors:
        for e in errors:
            print(f"llama_env_matrix: FAIL {e}", file=sys.stderr)
        return 1
    if args.check:
        print(f"llama_env_matrix: PASS envs={len(cfg['environments'])} appliances={len(cfg.get('appliances', []))}")
        return 0

    envs = select_envs(cfg, args.env, args.mode)
    if args.list:
        for env in envs:
            print(f"{env['id']}\t{env['family']}\tthreads={env['threads']}\tmode={env['mode']}\t{env['label']}")
        return 0

    plan = [item for env in envs for item in plan_for_env(cfg, env, args)]
    if args.execute:
        rows = [execute_item(item, args.timeout) for item in plan]
        action = "execute"
    else:
        rows = []
        for item in plan:
            status, reason = prerequisite_status(item)
            item["plan_status"] = item.get("status", "planned")
            item["status"] = status
            item["prerequisite_status"] = status
            if reason:
                item["blocker"] = reason
            rows.append(item)
        action = "dry-run"

    payload = {
        "schema_version": 1,
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "action": action,
        "selected_envs": [env["id"] for env in envs],
        "rows": rows,
        "families": sorted({row["family"] for row in rows}),
        "note": "blocked prerequisite rows are expected on hosts without CUDA/Vulkan/QEMU/Kraft images"
    }
    write_output(args.output, payload)
    print(f"llama_env_matrix: {action} rows={len(rows)} output={args.output}")
    for row in rows:
        app = f" single_app={row['single_app']}" if row.get("single_app") else ""
        print(f"- {row['env_id']} threads={row['threads']} status={row.get('status', row.get('prerequisite_status'))}{app}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
