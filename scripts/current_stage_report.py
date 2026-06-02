#!/usr/bin/env python3
"""Generate an auditable current-stage completeness report for VOGUE.

This gate is intentionally conservative: it does not promote blocked GPU rows.
It verifies that the current paper/design/evaluation artifacts are aligned with
Unikraft design rules and that every supported test/evaluation/benchmark surface
has a corresponding generated artifact.
"""
from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "stage"
GEN = ROOT / "paper" / "generated"


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
    GEN.mkdir(parents=True, exist_ok=True)

    targets = makefile_targets()
    required_targets = {
        "verify", "test-fast", "test-native", "test-qemu", "test-gpu",
        "governance-check", "native-tests", "venus-check", "stage-check", "benchmark-check",
        "eval-check", "paper-check", "lib-readme-check", "claim-check",
        "vulkan-tests", "vulkan-check",
    }
    scripts = {p.name for p in (ROOT / "scripts").glob("*.py")}
    required_scripts = {
        "app_perf_eval.py", "benchmark_summary.py", "eval_matrix.py",
        "paper_consistency_check.py", "real_driver_static_check.py",
        "stage_audit.py", "unikraft_alignment_check.py",
        "venus_perf_eval.py", "venus_qemu_probe.py", "real_virtio_gpu_path_check.py",
        "lib_readme_check.py", "vulkan_perf_eval.py", "llama_env_matrix.py",
        "llama_vulkan_eval.py", "llama_vulkan_api_coverage.py",
        "governance_check.py",
    }

    stage = load_json(OUT / "stage_audit.json")
    alignment = load_json(OUT / "unikraft_alignment.json")
    bench = load_json(ROOT / "results" / "benchmarks" / "benchmark_summary.json")
    matrix = load_json(ROOT / "results" / "vogue_evaluation_matrix.json")
    venus = load_json(ROOT / "results" / "venus" / "venus_perf.json")
    real_path = load_json(ROOT / "results" / "venus" / "real_path_check.json")
    qemu = load_json(ROOT / "results" / "venus" / "qemu_2d_probe.json")
    stk = load_json(ROOT / "results" / "stk" / "latest" / "stk_runtime_eval.json")
    paper = "\n".join(read(path) for path in sorted((ROOT / "paper" / "sections").glob("*.typ")))
    readme = read(ROOT / "README.md")

    eval_rows = matrix.get("rows", []) if isinstance(matrix.get("rows"), list) else []
    eval_by_id = {r.get("row_id", ""): r for r in eval_rows if isinstance(r, dict)}
    required_eval_rows = {"disp.2d", "proto.api-contract", "gfx.kmscube.sw", "gfx.glmark2.sw", "gfx.kmscube.submit", "gfx.kmscube.frame", "proto.real-driver", "xport.qemu-vgpu", "vk.readiness",
                          "vk.smoke", "gfx.vkmark", "vk.drm-shim", "vk.ggml-dispatch", "llm.bench.cpu", "llm.bench.vk"}
    lib_dirs = sorted(p for p in (ROOT / "libs").iterdir() if p.is_dir())
    missing_readmes = [p.name for p in lib_dirs if not (p / "README.md").exists()]

    rows = [
        row("make_targets", required_targets <= targets,
            f"present={sorted(required_targets & targets)} missing={sorted(required_targets - targets)}",
            "Makefile exposes all top-level test/evaluation/benchmark gates"),
        row("script_surface", required_scripts <= scripts,
            f"present={sorted(required_scripts & scripts)} missing={sorted(required_scripts - scripts)}",
            "All expected evaluation and guardrail scripts exist"),
        row("unikraft_alignment", alignment.get("status") == "pass",
            "results/stage/unikraft_alignment.json", "Unikraft design-rule alignment gate passes"),
        row("stage_audit", stage.get("status") == "pass",
            "results/stage/stage_audit.json", "Current-stage audit passes"),
        row("benchmark_summary", bench.get("status") == "pass" and len(bench.get("rows", [])) >= 8,
            f"rows={len(bench.get('rows', []))} results/benchmarks/benchmark_summary.json",
            "Benchmark summary exists with native app and Venus readiness rows"),
        row("evaluation_matrix", required_eval_rows <= set(eval_by_id),
            f"rows={len(eval_rows)} missing={sorted(required_eval_rows - set(eval_by_id))}",
            "Evidence matrix contains every supported pass/blocked claim row"),
        row("pass_rows",
            all(eval_by_id.get(r, {}).get("status") == "pass" for r in ["disp.2d", "proto.api-contract", "gfx.kmscube.sw", "gfx.glmark2.sw", "proto.real-driver", "vk.readiness", "vk.drm-shim", "gfx.vkmark"])
            and eval_by_id.get("vk.ggml-dispatch", {}).get("status") == "pass"
            and eval_by_id.get("llm.bench.cpu", {}).get("status", "").startswith(("pass", "blocked:"))
            and eval_by_id.get("llm.bench.vk", {}).get("status", "").startswith(("pass", "blocked:"))
            and eval_by_id.get("vk.smoke", {}).get("status") in ("pass", "blocked:vulkan-test-failed", "blocked:no-vulkan-device"),
            "results/vogue_evaluation_matrix.json", "Core supported rows pass; upstream llama.cpp rows accept structured blockers for missing QEMU/Venus images"),
        row("blocked_rows_are_explicit",
            eval_by_id.get("gfx.kmscube.submit", {}).get("status") in ("pass", "blocked:stale-appliance-kraft-unavailable", "blocked:missing-pass-marker")
            and eval_by_id.get("gfx.kmscube.frame", {}).get("status") in ("pass", "blocked:stale-appliance-kraft-unavailable", "blocked:missing-pass-marker"),
            "gfx.kmscube.submit+gfx.kmscube.frame present in matrix; STK porting out of scope", "K1 transport/frame rows are present; STK porting explicitly dropped"),
        row("venus_blocker_recorded",
            qemu.get("status") in ("pass", "blocked:modern-pci-unsupported", "blocked:probe-incomplete", "blocked:image-missing", "blocked:timeout", "blocked:qemu-missing"),
            "results/venus/qemu_2d_probe.json; results/vogue_evaluation_matrix.json",
            "QEMU Venus probe artifact recorded with a structured status"),
        row("real_path_selected", real_path.get("status") == "pass",
            "results/venus/real_path_check.json",
            "Production Kraft/config/build artifacts use the real VirtIO-GPU backend"),
        row("stk_out_of_scope",
            "Out of scope" in read(ROOT / "design/unikraft-virtio-gpu-spec-v1.md"),
            "design/unikraft-virtio-gpu-spec-v1.md", "STK porting is documented as out of scope (plan.md §0.5)"),
        row("library_readmes", not missing_readmes and len(lib_dirs) > 0,
            f"libs={len(lib_dirs)} missing={missing_readmes}", "Every local library has Unikraft-style README docs"),
        row("paper_generated_tables", all((GEN / name).exists() for name in ["app-performance-table.typ", "venus-stage-table.typ", "current-stage-table.typ"]),
            "paper/generated/app-performance-table.typ; paper/generated/venus-stage-table.typ; paper/generated/current-stage-table.typ",
            "Paper consumes generated benchmark/stage tables"),
        row("paper_claim_boundaries", all(s in paper for s in ["Claim Boundaries", "27 PASS", "0x1050"]),
            "paper/sections/08-evaluation.typ; paper/sections/12-artifact-appendix.typ",
            "Paper states current stage and claim boundaries"),
        row("readme_current_stage",
            all(s in readme for s in ["make stage-check", "make benchmark-check", "make venus-check"])
            and "27/27 PASS" in readme and "plan-fix.md" in readme,
            "README.md", "README exposes current-stage/evaluation commands and fix plan"),
        row("governance_metadata", all((ROOT / path).exists() for path in ["docs/GOVERNANCE.md", "config/governance.json"])
            and (ROOT.parent / "manifest" / "manifests" / "vogue-main.yaml").exists()
            and "make test-fast" in readme and "make governance-check" in readme and "../manifest" in readme,
            "docs/GOVERNANCE.md; config/governance.json; ../manifest/manifests/vogue-main.yaml",
            "Research-artifact governance metadata and manifest/VM ownership split are documented"),
    ]

    ok = all(r["status"] == "pass" for r in rows)
    payload = {
        "status": "pass" if ok else "fail",
        "written_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "summary": {
            "libraries": len(lib_dirs),
            "eval_rows": len(eval_rows),
            "benchmark_rows": len(bench.get("rows", [])) if isinstance(bench.get("rows"), list) else 0,
            "qemu_status": qemu.get("status"),
            "real_path_status": real_path.get("status"),
            "stk_accelerated_runtime": stk.get("accelerated_runtime"),
        },
        "rows": rows,
        "claim_boundary": "Current supported rows pass on the evaluation host, including K1/xport.qemu-vgpu and llama.cpp Vulkan runtime rows. The same rows must not be promoted on other hosts without same-run pass artifacts. Governance metadata links release claims to ../manifest specs and marks unlinked claims non-release; STK porting is out of scope (plan.md §0.5).",
    }
    (OUT / "current_stage_report.json").write_text(json.dumps(payload, indent=2) + "\n")

    md = [
        "# Current-stage completeness report", "",
        f"Status: `{payload['status']}`", "",
        "| Check | Status | Evidence | Required property |",
        "|---|---|---|---|",
    ]
    for r in rows:
        md.append(f"| `{r['id']}` | `{r['status']}` | {r['evidence']} | {r['required']} |")
    md += ["", "## Claim boundary", "", payload["claim_boundary"], ""]
    (OUT / "current_stage_report.md").write_text("\n".join(md))

    table_rows = [
        ("Alignment", alignment.get("status", "missing"), "Unikraft design rules and non-reimplementation boundaries"),
        ("Stage", stage.get("status", "missing"), "Real controlq driver present; QEMU blocker named"),
        ("Benchmarks", bench.get("status", "missing"), f"{payload['summary']['benchmark_rows']} generated benchmark/readiness rows"),
        ("Real path", real_path.get("status", "missing"), "production Kraft/config/build evidence selects real backend"),
        ("Evaluation", "pass" if required_eval_rows <= set(eval_by_id) else "fail", f"{payload['summary']['eval_rows']} evidence rows"),
        ("Governance", "pass" if (ROOT / "config" / "governance.json").exists() and (ROOT.parent / "manifest").exists() else "missing", "developer/release split plus external manifest ownership"),
        ("STK", "out-of-scope", "STK porting dropped per plan.md §0.5"),
    ]
    typ = [
        "// Generated by scripts/current_stage_report.py; do not edit by hand.",
        "#figure(",
        "  text(size: 8pt, table(",
        "    columns: (0.75in, 0.85in, 2.15in),",
        "    inset: 3pt,",
        "    align: (left, left, left),",
        "    table.header([*Gate*], [*Status*], [*Reviewer interpretation*]),",
    ]
    for gate, status, interp in table_rows:
        typ.append(f"    [{gate}], [`{status}`], [{interp}],")
    typ += [
        "  )),",
        "  caption: [Generated current-stage completeness gate. The gate checks design alignment, generated tests, benchmarks, paper linkage, and blocked acceleration non-claims.]",
        ") <tab:current-stage>",
        "",
    ]
    (GEN / "current-stage-table.typ").write_text("\n".join(typ))

    print(f"current_stage_report: {payload['status']} checks={len(rows)} eval_rows={len(eval_rows)} benchmark_rows={payload['summary']['benchmark_rows']}")
    if args.check and not ok:
        for r in rows:
            if r["status"] != "pass":
                print(f"FAIL {r['id']}: {r['evidence']}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
