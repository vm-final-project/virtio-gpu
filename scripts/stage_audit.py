#!/usr/bin/env python3
"""Produce an auditable current-stage report for VOGUE."""
from __future__ import annotations
import argparse, json, pathlib, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "stage"

def run(cmd: list[str]) -> dict:
    p=subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return {"cmd":" ".join(cmd), "returncode":p.returncode, "stdout_tail":p.stdout[-6000:]}

def load_json(path: pathlib.Path) -> dict:
    try: return json.loads(path.read_text())
    except Exception as e: return {"status":"missing", "error":str(e), "path":str(path)}

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument("--check", action="store_true"); args=ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    commands=[
        ["make","-C","tests","proto-abi"],
        ["python3","scripts/check_venus_vulkan_docs.py"],
        ["python3","scripts/real_driver_static_check.py"],
        ["python3","scripts/unikraft_alignment_check.py"],
        ["python3","scripts/venus_qemu_probe.py","--mode","2d","--allow-blocked"],
        ["python3","scripts/venus_perf_eval.py","--repetitions","5","--allow-blocked"],
    ]
    runs=[run(c) for c in commands]
    qemu=load_json(ROOT/"results/venus/qemu_2d_probe_latest.json")
    venus_perf=load_json(ROOT/"results/venus/venus_perf_latest.json")
    eval_matrix=load_json(ROOT/"results/vogue_latest_evaluation_matrix.json")
    align=load_json(OUT/"unikraft_alignment_latest.json")
    required_docs=["design/unikraft-virtio-gpu-spec-v1.md","design/virtio-gpu-vulken-v1.md","README.md","paper/sections/08-evaluation.typ"]
    doc_status={p:(ROOT/p).exists() for p in required_docs}
    stage="real-driver-controlq-implemented; qemu-venus-blocked-modern-pci" if qemu.get("status") == "blocked:modern-pci-unsupported" else qemu.get("status","unknown")
    ok=all(r["returncode"]==0 for r in runs) and all(doc_status.values()) and align.get("status") == "pass"
    payload={
        "status":"pass" if ok else "fail", "stage":stage,
        "written_at":time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "runs":runs, "documents":doc_status,
        "qemu_probe_status":qemu.get("status"),
        "venus_perf_status":venus_perf.get("status"),
        "venus_acceleration_status":venus_perf.get("acceleration_status"),
        "eval_rows":[r.get("row_id")+":"+r.get("status") for r in eval_matrix.get("rows",[]) if isinstance(r,dict)],
        "claim_boundary":"Native/static/design/paper gates pass. Real QEMU Venus acceleration remains blocked unless qemu_probe_status is pass.",
    }
    (OUT/"stage_audit_latest.json").write_text(json.dumps(payload, indent=2)+"\n")
    md=["# VOGUE stage audit", "", f"Status: `{payload['status']}`", f"Stage: `{stage}`", "", "## Required artifacts", ""]
    for p, exists in doc_status.items(): md.append(f"- `{p}`: {'present' if exists else 'missing'}")
    md += ["", "## Key statuses", "", f"- QEMU Venus probe: `{payload['qemu_probe_status']}`", f"- Venus perf gate: `{payload['venus_perf_status']}`", f"- Acceleration status: `{payload['venus_acceleration_status']}`", "", "## Claim boundary", payload["claim_boundary"], ""]
    (OUT/"stage_audit_latest.md").write_text("\n".join(md)+"\n")
    print(f"stage_audit: {payload['status']} stage={stage}")
    return 0 if ok or not args.check else 1
if __name__ == "__main__":
    raise SystemExit(main())
