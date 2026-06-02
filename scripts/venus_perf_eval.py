#!/usr/bin/env python3
"""Performance/evaluation artifact generator for the Venus gate.

When the local stack cannot run Venus yet, this still emits a structured
BLOCKED row so downstream reports do not confuse software/fake evidence with
acceleration evidence.
"""
from __future__ import annotations
import argparse, csv, json, pathlib, statistics, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "venus"

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repetitions", type=int, default=30)
    ap.add_argument("--allow-blocked", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    latencies=[]
    statuses=[]
    for _ in range(max(1,args.repetitions)):
        t0=time.perf_counter_ns()
        proc=subprocess.run(["python3", "scripts/real_driver_static_check.py"], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        latencies.append((time.perf_counter_ns()-t0)/1e6)
        statuses.append(proc.returncode)
    qemu_json = OUT / "qemu_2d_probe.json"
    qemu_status = "not-run"
    if qemu_json.exists():
        qemu_status = json.loads(qemu_json.read_text()).get("status", "unknown")
    status = "pass" if all(s == 0 for s in statuses) else "fail"
    acceleration_status = "blocked:host-visible-or-qemu-gate" if qemu_status != "pass" else "ready-for-venus-smoke"
    data={
        "status": status,
        "acceleration_status": acceleration_status,
        "repetitions": len(latencies),
        "static_gate_latency_ms": {
            "mean": statistics.mean(latencies), "median": statistics.median(latencies),
            "min": min(latencies), "max": max(latencies),
            "p95": sorted(latencies)[int(0.95*(len(latencies)-1))],
        },
        "qemu_probe_status": qemu_status,
        "claim_boundary": "Static/native gates are measured here; STK/Vulkan acceleration is blocked until QEMU Venus probe is pass.",
        "written_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    (OUT/"venus_perf.json").write_text(json.dumps(data, indent=2)+"\n")
    with (OUT/"venus_perf.csv").open("w", newline="") as f:
        w=csv.DictWriter(f, fieldnames=["metric","value"], lineterminator="\n"); w.writeheader()
        for k,v in data["static_gate_latency_ms"].items(): w.writerow({"metric":f"static_gate_latency_ms_{k}", "value":v})
        w.writerow({"metric":"qemu_probe_status", "value":qemu_status})
        w.writerow({"metric":"acceleration_status", "value":acceleration_status})
    (OUT/"venus_perf.md").write_text("# Venus performance/evaluation gate\n\n"+json.dumps(data,indent=2)+"\n")
    print(f"venus_perf_eval: {status} acceleration={acceleration_status} qemu={qemu_status}")
    return 0 if status == "pass" and (args.allow_blocked or acceleration_status == "ready-for-venus-smoke") else 1
if __name__ == "__main__":
    raise SystemExit(main())
