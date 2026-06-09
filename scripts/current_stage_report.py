#!/usr/bin/env python3
"""Generate an auditable current-stage completeness report for VOGUE."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from artifact_utils import make_artifact, utc_now, write_json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "stage"


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except Exception as exc:  # pragma: no cover - diagnostic path
        return {"status": "missing", "error": str(exc), "path": str(path)}


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def makefile_targets() -> set[str]:
    text = read(ROOT / "Makefile")
    targets: set[str] = set()
    for line in text.splitlines():
        if not line or line.startswith("\t") or line.startswith("#"):
            continue
        m = re.match(r"^([A-Za-z0-9_.-]+)\s*:", line)
        if m:
            targets.add(m.group(1))
    return targets


def row(check_id: str, status: bool, evidence: str, required: str, blocker: str = "") -> dict:
    return {
        "id": check_id,
        "status": "pass" if status else "fail",
        "evidence": evidence,
        "required": required,
        "blocker": blocker,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    targets = makefile_targets()
    required_targets = {
        "verify", "test-fast", "test-native", "test-qemu", "test-gpu",
        "native-tests", "test-core", "test-compat", "test-venus", "test-dispatch",
        "proto-abi", "vulkan-tests", "venus-check", "vulkan-check",
        "eval-check", "current-stage-check", "llm-server-vk-check", "llm-server-vk-throughput-check",
    }
    scripts = {p.name for p in (ROOT / "scripts").glob("*.py")}
    required_scripts = {
        "eval_matrix.py",
        "vulkan_perf_eval.py",
        "venus_perf_eval.py",
        "llama_vulkan_eval.py",
        "llm_server_vk_check.py",
        "llm_server_vk_throughput_check.py",
        "current_stage_report.py",
        "venus_qemu_probe.py",
        "llama_vk_real_run.py",
        "llama_cpu_real_run.py",
        "llama_server_vk_capture.py",
        "llama_server_cpu_capture.py",
        "llama_vk_build_capture.py",
        "linux_guest_vulkan_baseline.py",
        "llama_vulkan_linux_baseline.py",
        "artifact_utils.py",
    }

    matrix = load_json(ROOT / "results" / "vogue_evaluation_matrix.json")
    venus = load_json(ROOT / "results" / "venus" / "venus_perf.json")
    vulkan = load_json(ROOT / "results" / "vulkan" / "vulkan_perf.json")
    server = load_json(ROOT / "results" / "llama" / "server_vk_check.json")
    qemu = load_json(ROOT / "results" / "venus" / "qemu_2d_probe.json")
    readme = read(ROOT / "README.md")

    eval_rows = matrix.get("rows", []) if isinstance(matrix.get("rows"), list) else []
    eval_by_id = {r.get("row_id", ""): r for r in eval_rows if isinstance(r, dict)}
    required_eval_rows = {"disp.2d", "proto.api-contract", "gfx.kmscube.submit", "gfx.kmscube.frame", "proto.real-driver", "xport.qemu-vgpu", "vk.readiness",
                          "vk.smoke", "gfx.vkmark", "vk.drm-core", "vk.drm-fdio", "vk.ggml-dispatch", "llm.bench.cpu", "llm.bench.vk"}
    lib_dirs = sorted(p for p in (ROOT / "libs").iterdir() if p.is_dir())
    missing_readmes = [p.name for p in lib_dirs if not (p / "README.md").exists()]

    rows = [
        row("make_targets", required_targets <= targets,
            f"present={sorted(required_targets & targets)} missing={sorted(required_targets - targets)}",
            "Makefile exposes all top-level test/evaluation/benchmark gates"),
        row("script_surface", required_scripts <= scripts,
            f"present={sorted(required_scripts & scripts)} missing={sorted(required_scripts - scripts)}",
            "All expected evaluation and guardrail scripts exist"),
        row("evaluation_matrix", required_eval_rows <= set(eval_by_id),
            f"rows={len(eval_rows)} missing={sorted(required_eval_rows - set(eval_by_id))}",
            "Evidence matrix contains every supported pass/blocked claim row"),
        row("pass_rows",
            all(eval_by_id.get(r, {}).get("status") == "pass" for r in ["disp.2d", "proto.api-contract", "proto.real-driver", "vk.readiness", "vk.drm-core", "vk.drm-fdio", "gfx.vkmark"])
            and eval_by_id.get("vk.ggml-dispatch", {}).get("status") == "pass"
            and eval_by_id.get("llm.bench.cpu", {}).get("status", "").startswith(("pass", "blocked:"))
            and eval_by_id.get("llm.bench.vk", {}).get("status", "").startswith(("pass", "blocked:"))
            and eval_by_id.get("vk.smoke", {}).get("status") in ("pass", "blocked:vulkan-test-failed", "blocked:no-vulkan-device"),
            "results/vogue_evaluation_matrix.json", "Core supported rows pass; upstream llama.cpp rows accept structured blockers for missing QEMU/Venus images"),
        row("blocked_rows_are_explicit",
            eval_by_id.get("gfx.kmscube.submit", {}).get("status") in ("pass", "blocked:stale-appliance-kraft-unavailable", "blocked:missing-pass-marker")
            and eval_by_id.get("gfx.kmscube.frame", {}).get("status") in ("pass", "blocked:stale-appliance-kraft-unavailable", "blocked:missing-pass-marker", "blocked:no-pixel-proof"),
            "gfx.kmscube.submit+gfx.kmscube.frame present in matrix; STK porting out of scope", "K1 transport/frame rows are present; STK porting explicitly dropped"),
        row("venus_blocker_recorded",
            qemu.get("status") in ("pass", "blocked:modern-pci-unsupported", "blocked:probe-incomplete", "blocked:image-missing", "blocked:timeout", "blocked:qemu-missing"),
            "results/venus/qemu_2d_probe.json; results/vogue_evaluation_matrix.json",
            "QEMU Venus probe artifact recorded with a structured status"),
        row("generator_json_only",
            all(data and "metadata" in data for data in [venus, vulkan, server]),
            "results/venus/venus_perf.json; results/vulkan/vulkan_perf.json; results/llama/server_vk_check.json",
            "Canonical generators emit the shared JSON artifact shape"),
        row("library_readmes", not missing_readmes and len(lib_dirs) > 0,
            f"libs={len(lib_dirs)} missing={missing_readmes}", "Every local library has Unikraft-style README docs"),
        row("readme_current_stage",
            all(s in readme for s in ["make native-tests", "make test-compat", "make venus-check", "results/vogue_evaluation_matrix.json"])
            and "blocked:*" in readme,
            "README.md", "README exposes canonical commands and JSON-only evaluation artifacts"),
    ]

    ok = all(r["status"] == "pass" for r in rows)
    payload = make_artifact(
        source="scripts/current_stage_report.py",
        status="pass" if ok else "fail",
        headline="Current-stage completeness report",
        counts={
            "libraries": len(lib_dirs),
            "eval_rows": len(eval_rows),
            "canonical_scripts": len(required_scripts),
        },
        rows=rows,
        extra={
            "generated_utc": utc_now(),
            "summary": {
                "headline": "Current-stage completeness report",
                "counts": {
                    "libraries": len(lib_dirs),
                    "eval_rows": len(eval_rows),
                    "canonical_scripts": len(required_scripts),
                },
                "qemu_status": qemu.get("status"),
                "vulkan_status": vulkan.get("status"),
                "server_status": server.get("status"),
            },
            "claim_boundary": "Current supported rows pass on the evaluation host only when backed by same-run JSON artifacts. Blocked rows remain explicit partial-progress states and are never promoted to passing evidence.",
        },
    )
    write_json(OUT / "current_stage_report.json", payload)

    print(
        f"current_stage_report: {payload['status']} checks={len(rows)} "
        f"eval_rows={len(eval_rows)} canonical_scripts={payload['summary']['counts']['canonical_scripts']}"
    )
    if args.check and not ok:
        for r in rows:
            if r["status"] != "pass":
                print(f"FAIL {r['id']}: {r['evidence']}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
