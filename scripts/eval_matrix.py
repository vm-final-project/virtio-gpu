#!/usr/bin/env python3
"""Validate canonical result JSON without treating blocked rows as passes."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULT_DIRS = (
    ROOT / "results/llama",
    ROOT / "results/venus",
    ROOT / "results/smp",
)


def iter_result_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_file() and path.suffix == ".json":
            files.append(path)
        elif path.is_dir():
            files.extend(sorted(path.glob("*.json")))
    return sorted(files)


def classify(status: str) -> str:
    if status == "pass":
        return "pass"
    if status.startswith("blocked:"):
        return "blocked"
    return "invalid"


def validate_pass_row(path: Path, payload: dict, release: bool = False) -> str | None:
    if path.name.endswith("_matrix.json"):
        return None

    if not path.name.startswith("llama_") or "server" in path.name:
        return None

    inputs = payload.get("inputs", {})
    if not isinstance(inputs, dict):
        return "passing llama bench row must contain inputs object"

    metrics = payload.get("metrics")
    if not isinstance(metrics, dict):
        return "passing llama bench row must contain metrics object"

    bench_kind = metrics.get("bench_kind") or inputs.get("bench_kind")
    if bench_kind not in ("hand_rolled_smoke", "upstream_llama_bench"):
        return "passing llama bench row must declare bench_kind"

    if bench_kind == "upstream_llama_bench":
        for key in ("pp", "tg", "threads", "backend"):
            if key not in metrics:
                return f"upstream_llama_bench row missing metrics.{key}"

    backend = metrics.get("backend") or inputs.get("backend")
    if release and (backend == "vulkan" or "_vk" in path.name) and bench_kind != "upstream_llama_bench":
        return "release Vulkan pass rows must use upstream_llama_bench"

    if backend == "vulkan" or "_vk" in path.name:
        for key in ("qemu_mem_mb", "gpu_hostmem", "egl_rendernode",
                    "renderer_expect", "model_size_bytes"):
            if key not in inputs:
                return f"passing llama-vk row missing inputs.{key}"
        if metrics.get("backend") != "vulkan" and inputs.get("backend") != "vulkan":
            return "passing llama-vk row must declare backend=vulkan"

    return None


def check_results(files: list[Path], release: bool = False) -> tuple[int, dict]:
    summary = {"pass": [], "blocked": [], "invalid": []}
    for path in files:
        try:
            payload = json.loads(path.read_text())
        except Exception as exc:
            summary["invalid"].append({"path": str(path), "error": f"json:{exc}"})
            continue

        status = payload.get("status")
        bucket = classify(status) if isinstance(status, str) else "invalid"
        row = {"path": str(path), "status": status}
        if bucket == "invalid":
            row["error"] = "status must be pass or blocked:<reason>"
        elif bucket == "pass":
            error = validate_pass_row(path, payload, release=release)
            if error:
                row["error"] = error
                summary["invalid"].append(row)
                continue
        summary[bucket].append(row)

    return (0 if not summary["invalid"] else 1), summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero on malformed result rows")
    parser.add_argument("--release", action="store_true",
                        help="require release-grade evidence rows")
    parser.add_argument("paths", nargs="*", type=Path,
                        help="result files or directories; defaults to canonical result dirs")
    args = parser.parse_args()

    files = iter_result_files(args.paths or list(DEFAULT_RESULT_DIRS))
    rc, summary = check_results(files, release=args.release)
    print(json.dumps({
        "files": len(files),
        "pass": len(summary["pass"]),
        "blocked": len(summary["blocked"]),
        "invalid": summary["invalid"],
    }, indent=2))
    return rc if args.check else 0


if __name__ == "__main__":
    raise SystemExit(main())
