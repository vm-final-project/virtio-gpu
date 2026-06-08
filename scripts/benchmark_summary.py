#!/usr/bin/env python3
"""Aggregate supported benchmark/evaluation artifacts into JSON/CSV/MD."""
from __future__ import annotations
import argparse, csv, json, pathlib, time

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"results"/"benchmarks"

def load(path: pathlib.Path) -> dict:
    try: return json.loads(path.read_text())
    except Exception as e: return {"status":"missing", "error":str(e), "path":str(path)}

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument("--check", action="store_true"); args=ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    app=load(ROOT/"results/app_perf.json")
    venus=load(ROOT/"results/venus/venus_perf.json")
    qemu=load(ROOT/"results/venus/qemu_2d_probe.json")
    rows=[]
    for r in app.get("rows",[]):
        if isinstance(r,dict):
            rows.append({"row_id":r.get("row_id"), "benchmark":r.get("app"), "status":r.get("status"), "metric":"avg_frame_ms", "value":r.get("avg_frame_ms"), "scope":"native software/substrate fake-backend"})
            rows.append({"row_id":r.get("row_id"), "benchmark":r.get("app"), "status":r.get("status"), "metric":"fps", "value":r.get("fps"), "scope":"native software/substrate fake-backend"})
    vlat=venus.get("static_gate_latency_ms",{}) if isinstance(venus.get("static_gate_latency_ms"),dict) else {}
    for k in ["mean","median","p95"]:
        rows.append({"row_id":"VSTAT", "benchmark":"real driver static gate", "status":venus.get("status","missing"), "metric":f"latency_ms_{k}", "value":vlat.get(k), "scope":"static gate overhead, not GPU performance"})
    rows.append({"row_id":"xport.qemu-vgpu", "benchmark":"QEMU Venus probe", "status":qemu.get("status","missing"), "metric":"probe_status", "value":qemu.get("status"), "scope":"real QEMU probe; pass required for acceleration claims"})
    ok=bool(app.get("rows")) and venus.get("status") == "pass" and qemu.get("status") in {"pass","blocked:modern-pci-unsupported","blocked:probe-incomplete","blocked:qemu-missing","blocked:image-missing","blocked:timeout"}
    payload={"status":"pass" if ok else "fail", "written_at":time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "rows":rows, "claim_boundary":"Only native software/substrate rows are performance PASS rows. Venus/QEMU rows are readiness/blocker measurements unless xport.qemu-vgpu is pass."}
    (OUT/"benchmark_summary.json").write_text(json.dumps(payload, indent=2)+"\n")
    with (OUT/"benchmark_summary.csv").open("w", newline="") as f:
        w=csv.DictWriter(f, fieldnames=["row_id","benchmark","status","metric","value","scope"], lineterminator="\n"); w.writeheader(); w.writerows(rows)
    md=["# VOGUE benchmark summary", "", f"Status: `{payload['status']}`", "", "| Row | Benchmark | Status | Metric | Value | Scope |", "|---|---|---|---|---|---|"]
    for r in rows: md.append(f"| `{r['row_id']}` | {r['benchmark']} | `{r['status']}` | `{r['metric']}` | `{r['value']}` | {r['scope']} |")
    (OUT/"benchmark_summary.md").write_text("\n".join(md)+"\n")
    print(f"benchmark_summary: {payload['status']} rows={len(rows)}")
    return 0 if ok or not args.check else 1
if __name__ == "__main__":
    raise SystemExit(main())
