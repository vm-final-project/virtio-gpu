#!/usr/bin/env python3
"""Capture the host Linux Vulkan/Venus llama.cpp baseline on the evaluation host.

`host.baseline.vk` is the standards-track reference for the Unikraft Vulkan rows:
the SAME upstream ggml-vulkan binary and GGUF model the Unikraft port targets,
run on the host's real GPU through the host Mesa Vulkan driver. It runs
`llama.cpp/build-vk/bin/llama-bench -ngl 99` and records pp512 / tg128.

This is an honest, same-host measurement (no fabricated numbers, no foreign-host
baseline JSON). It is the denominator for `host.bench.vk` and gives reviewers an
apples-to-apples gap between bare-metal Vulkan and the Unikraft Venus path.

Resolution order for the model: $VOGUE_VK_MODEL, $VOGUE_CPU_MODEL,
config/llama_env_matrix.json default_path, else the first GGUF under ../models.
The llama-bench binary is $VOGUE_LLAMA_BENCH or llama.cpp/build-vk/bin/llama-bench
(LLAMA_ROOT honoured).
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / "results" / "llama"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _llama_root() -> Path:
    env = os.environ.get("LLAMA_ROOT")
    if env:
        return Path(env)
    try:
        cfg = json.loads((ROOT / "config" / "external_paths.json").read_text())
        return (ROOT / cfg["paths"]["LLAMA_ROOT"]["default"]).resolve()
    except (OSError, KeyError, json.JSONDecodeError):
        return (ROOT.parent / "llama.cpp").resolve()


def _bench_bin() -> Path | None:
    explicit = os.environ.get("VOGUE_LLAMA_BENCH")
    if explicit and Path(explicit).is_file():
        return Path(explicit)
    cand = _llama_root() / "build-vk" / "bin" / "llama-bench"
    return cand if cand.is_file() else None


def _model_path() -> Path | None:
    for env in ("VOGUE_VK_MODEL", "VOGUE_CPU_MODEL"):
        v = os.environ.get(env)
        if v and Path(v).is_file():
            return Path(v)
    try:
        cfg = json.loads((ROOT / "config" / "llama_env_matrix.json").read_text())
        cand = ROOT / cfg["model"]["default_path"]
        if cand.is_file():
            return cand
    except (OSError, KeyError, json.JSONDecodeError):
        pass
    for p in (ROOT.parent / "models").rglob("*.gguf"):
        return p
    return None


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "vulkan_linux_baseline.json").write_text(
        json.dumps(payload, indent=1) + "\n")


def _blocked(reason: str, **extra) -> int:
    _write({
        "generated_utc": _now(),
        "evidence_id": "llama-vk-linux",
        "status": reason,
        "claim_allowed": "No host baseline claim; documented blocker only.",
        "claim_forbidden": "Unikraft-internal Vulkan compute claim — that gate is LLAMA-VK-RUN.",
        **extra,
    })
    print(f"llama-vulkan-linux-baseline: {reason}")
    return 0


def main() -> int:
    bench = _bench_bin()
    if bench is None:
        return _blocked("blocked:llama-bench-vk-missing",
                        hint="Build llama.cpp/build-vk with -DGGML_VULKAN=1.")
    model = _model_path()
    if model is None:
        return _blocked("blocked:model-missing")

    env = {**os.environ}
    # llama-bench dlopens nothing extra, but the Vulkan ggml backend needs the
    # build-vk libs on the loader path on some layouts.
    libdir = str(bench.parent)
    env["LD_LIBRARY_PATH"] = libdir + os.pathsep + env.get("LD_LIBRARY_PATH", "")

    cmd = [str(bench), "-m", str(model), "-ngl", "99", "-p", "512", "-n", "128"]
    try:
        r = subprocess.run(cmd, text=True, capture_output=True, timeout=600, env=env)
        out = (r.stdout or "") + (r.stderr or "")
    except (subprocess.TimeoutExpired, OSError) as e:
        return _blocked("blocked:llama-bench-failed", error=str(e))

    dev = re.search(r"ggml_vulkan:\s*0\s*=\s*([^|]+?)\s*\(", out)
    pp = re.search(r"\bpp512\b\s*\|\s*([0-9.]+)", out)
    tg = re.search(r"\btg128\b\s*\|\s*([0-9.]+)", out)
    if not (pp and tg):
        return _blocked("blocked:no-throughput-parsed", output_tail=out[-2000:])

    payload = {
        "generated_utc": _now(),
        "evidence_id": "llama-vk-linux",
        "status": "pass",
        "device": dev.group(1).strip() if dev else "unknown",
        "gpu_info": dev.group(1).strip() if dev else "unknown",
        "model_filename": model.name,
        "n_gpu_layers": 99,
        "pp512_t_per_s": float(pp.group(1)),
        "tg128_t_per_s": float(tg.group(1)),
        "backend": "Vulkan (Mesa Venus ICD path / host driver)",
        "llama_bench": str(bench),
        "command": cmd,
        "claim_allowed": ("Linux-host Vulkan baseline measured against the same upstream "
                          "ggml-vulkan binary and GGUF the Unikraft Venus port uses, on the "
                          "evaluation host GPU."),
        "claim_forbidden": "Unikraft-internal Vulkan compute claim — that gate is LLAMA-VK-RUN.",
    }
    _write(payload)
    print(f"llama-vulkan-linux-baseline: pass device={payload['device']!r} "
          f"pp512={payload['pp512_t_per_s']} tg128={payload['tg128_t_per_s']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
