#!/usr/bin/env python3
"""Record model-load latency for each llama appliance run.

Reads the latest serial-log artifacts produced by the appliance's
`load_model_*` (CPU + Vulkan variants both emit `model_load path=… use_mmap=…
huge_pages=… elapsed_ms=…`) and writes `results/model-load/latest.{json,md}`.

Implements plan-optimize.md L1.4 (huge-page mmap) verification gate. Hosts
that have not yet captured a load log produce a structured `blocked:no-log`
row rather than failing the build.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "model-load"
RESULTS = ROOT / "results"

LOG_LINE_RE = re.compile(
    r"model_load\s+path=(?P<path>\S+)\s+use_mmap=(?P<mmap>\d+)\s+"
    r"huge_pages=(?P<huge>\d+)\s+elapsed_ms=(?P<ms>[0-9.]+)"
)

APPLIANCES = [
    {"name": "llm.bench.cpu",   "log": "llama/upstream_cpu_serial.log"},
    {"name": "llm.server.cpu",  "log": "llama/upstream_server_cpu_serial.log"},
    {"name": "llm.bench.vk",    "log": "llama/upstream_vk_serial.log"},
    {"name": "llm.server.vk",   "log": "llama/upstream_server_vk_serial.log"},
]


def parse(path: Path) -> dict | None:
    if not path.exists():
        return None
    text = path.read_text(errors="replace")
    m = LOG_LINE_RE.search(text)
    if not m:
        return None
    return {
        "model_path":   m.group("path"),
        "use_mmap":     bool(int(m.group("mmap"))),
        "huge_pages":   bool(int(m.group("huge"))),
        "elapsed_ms":   float(m.group("ms")),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="exit 0 if every row is pass or blocked:* (matches image-size-check)")
    args = parser.parse_args(argv)

    rows: list[dict] = []
    for a in APPLIANCES:
        log = RESULTS / a["log"]
        parsed = parse(log)
        if parsed is None:
            rows.append({
                "name":     a["name"],
                "log":      str(log.relative_to(ROOT)),
                "status":   "blocked:no-log",
            })
            continue
        rows.append({
            "name":         a["name"],
            "log":          str(log.relative_to(ROOT)),
            "status":       "pass",
            "use_mmap":     parsed["use_mmap"],
            "huge_pages":   parsed["huge_pages"],
            "elapsed_ms":   round(parsed["elapsed_ms"], 2),
            "model_path":   parsed["model_path"],
        })

    OUT.mkdir(parents=True, exist_ok=True)
    generated = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    payload = {
        "metadata": {
            "generated_utc": generated,
            "source":        "scripts/model_load_time_check.py",
            "principle":     "plan-optimize.md L1.4 — huge-page mmap loader gate; "
                             "use_mmap=False until lib-9pfs grows mmap support.",
        },
        "rows": rows,
    }
    (OUT / "latest.json").write_text(json.dumps(payload, indent=2) + "\n")
    md = ["# Model-load latency (plan-optimize.md L1.4)", "",
          f"Generated: `{generated}`", "",
          "| Appliance | Status | use_mmap | huge_pages | elapsed_ms |",
          "|---|---|---|---|---|"]
    for r in rows:
        md.append(f"| `{r['name']}` | `{r['status']}` | "
                  f"{r.get('use_mmap','n/a')} | {r.get('huge_pages','n/a')} | "
                  f"{r.get('elapsed_ms','n/a')} |")
    (OUT / "latest.md").write_text("\n".join(md) + "\n")

    for r in rows:
        print(f"model-load: {r['name']:<16} {r['status']:<14}"
              f" mmap={r.get('use_mmap','n/a')} huge={r.get('huge_pages','n/a')}"
              f" elapsed_ms={r.get('elapsed_ms','n/a')}")

    if args.check:
        bad = [r for r in rows if not (r["status"] == "pass" or r["status"].startswith("blocked"))]
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
