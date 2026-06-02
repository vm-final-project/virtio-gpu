#!/usr/bin/env python3
"""Regression gate for VOGUE performance metrics.

Reads `config/perf_baseline.json`, pulls current numbers from the same
artifacts produced by `make app-perf-check` and `make llama-upstream-cpu-run`,
and fails (non-zero exit) when any metric regresses by more than
`regression_threshold_pct`. Missing artifacts produce a structured
`blocked:*` row rather than a hard failure, mirroring image-size-check.

Writes `results/perf/report.json` and `.md`.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "config" / "perf_baseline.json"
OUT = ROOT / "results" / "perf"

# Mapping: row id -> list of (metric, source_file, json_pointer-ish)
SOURCES = {
    "gfx.kmscube.sw": [
        ("fps", "results/app_perf.json",
         lambda d: _find_row(d, "gfx.kmscube.sw", "fps")),
        ("frame_ms", "results/app_perf.json",
         lambda d: _find_row(d, "gfx.kmscube.sw", "avg_frame_ms")),
    ],
    "gfx.glmark2.sw": [
        ("fps", "results/app_perf.json",
         lambda d: _find_row(d, "gfx.glmark2.sw", "fps")),
        ("frame_ms", "results/app_perf.json",
         lambda d: _find_row(d, "gfx.glmark2.sw", "avg_frame_ms")),
    ],
    "llm.bench.cpu": [
        ("pp512", "results/llama/upstream_cpu.json",
         lambda d: d.get("pp512")),
        ("tg128", "results/llama/upstream_cpu.json",
         lambda d: d.get("tg128")),
    ],
}


def _find_row(data: dict, row_id: str, key: str):
    for r in data.get("rows", []):
        if isinstance(r, dict) and r.get("row_id") == row_id:
            return r.get(key)
    return None


def _load(path: Path):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        return None
    except json.JSONDecodeError:
        return None


def _pct_delta(current: float, baseline: float) -> float:
    if baseline == 0:
        return math.inf if current else 0.0
    return ((current - baseline) / baseline) * 100.0


def collect(baseline: dict) -> tuple[list[dict], bool]:
    threshold = float(baseline.get("regression_threshold_pct", 3.0))
    rows: list[dict] = []
    any_pass = False
    for row_id, metric_defs in SOURCES.items():
        row = baseline["metrics"].get(row_id, {})
        for metric, source_rel, accessor in metric_defs:
            source_path = ROOT / source_rel
            data = _load(source_path)
            current = accessor(data) if data is not None else None
            base = row.get(metric)
            if current is None or base is None:
                rows.append({
                    "row": row_id,
                    "metric": metric,
                    "status": "blocked:source-missing",
                    "current": current,
                    "baseline": base,
                    "delta_pct": None,
                    "source": source_rel,
                })
                continue
            # For latency metrics ('frame_ms', 'ms') lower is better; invert sign.
            inverted = metric.endswith("_ms") or metric == "latency_ms"
            raw = _pct_delta(float(current), float(base))
            delta = -raw if inverted else raw
            regression = delta < -threshold
            any_pass = any_pass or not regression
            rows.append({
                "row": row_id,
                "metric": metric,
                "status": "regression" if regression else "pass",
                "current": current,
                "baseline": base,
                "delta_pct": round(delta, 2),
                "threshold_pct": threshold,
                "source": source_rel,
            })
    return rows, any_pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="non-zero exit when any metric regresses")
    parser.add_argument("--allow-blocked", action="store_true",
                        help="missing source artifacts produce a blocker row but exit 0")
    args = parser.parse_args(argv)

    baseline = _load(BASELINE)
    if baseline is None:
        print(f"perf_check: FAIL missing {BASELINE.relative_to(ROOT)}")
        return 1
    rows, any_pass = collect(baseline)
    OUT.mkdir(parents=True, exist_ok=True)
    generated = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    payload = {
        "metadata": {
            "generated_utc": generated,
            "source": "scripts/perf_check.py",
            "regression_threshold_pct": baseline.get("regression_threshold_pct"),
            "baseline_captured_utc": baseline.get("captured_utc"),
        },
        "rows": rows,
    }
    (OUT / "latest.json").write_text(json.dumps(payload, indent=2) + "\n")
    md = ["# Performance regression matrix", "", f"Generated: `{generated}`",
          "",
          "| Row | Metric | Status | Current | Baseline | Δ% | Source |",
          "|---|---|---|---|---|---|---|"]
    for r in rows:
        md.append(
            f"| `{r['row']}` | {r['metric']} | `{r['status']}` "
            f"| {r['current']} | {r['baseline']} | {r['delta_pct']} | `{r['source']}` |"
        )
    (OUT / "latest.md").write_text("\n".join(md) + "\n")

    regressions = [r for r in rows if r["status"] == "regression"]
    blocked = [r for r in rows if r["status"].startswith("blocked")]
    for r in rows:
        print(f"perf-check: {r['row']:<18} {r['metric']:<10} {r['status']:<22}"
              f" current={r['current']} baseline={r['baseline']} delta={r['delta_pct']}%")
    if args.check and regressions:
        return 1
    if args.check and blocked and not (any_pass or args.allow_blocked):
        # Everything was blocked → still exit 0 only if --allow-blocked.
        print(f"perf-check: blocked ({len(blocked)} rows); pass --allow-blocked to ignore")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
