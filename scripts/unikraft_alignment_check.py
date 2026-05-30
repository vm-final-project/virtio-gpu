#!/usr/bin/env python3
"""Check VOGUE's current design against Unikraft-oriented project rules."""
from __future__ import annotations
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "stage"

CHECKS = [
    ("modular_libukvirtio_gpu", ROOT / "libs/libukvirtio_gpu/Config.uk", ["config LIBUKVIRTIO_GPU", "LIBUKVIRTIO_GPU_BACKEND_REAL"], "VirtIO-GPU is a selectable micro-library; fake backend lives in tests/ only."),
    ("reuse_unikraft_virtio", ROOT / "libs/libukvirtio_gpu/virtio_gpu_real.c", ["<virtio/virtio_bus.h>", "<virtio/virtqueue.h>", "VIRTIO_BUS_REGISTER_DRIVER"], "Real backend uses Unikraft virtio bus/virtqueue instead of project-local PCI/queue reimplementation."),
    ("reuse_unikraft_alloc", ROOT / "libs/libukvirtio_gpu/virtio_gpu_real.c", ["uk_malloc", "uk_calloc", "uk_free"], "Real backend uses Unikraft allocator APIs."),
    ("fail_closed_host_visible", ROOT / "libs/libukvirtio_gpu/virtio_gpu_real.c", ["Current local Unikraft virtio-pci exposes no shared-memory BAR helper", "return -ENOTSUP"], "Host-visible blob mapping is truthfully blocked when the Unikraft transport lacks SHM BAR exposure."),
    ("stk_out_of_scope", ROOT / "design/unikraft-virtio-gpu-spec-v1.md", ["Out of scope"], "STK porting is explicitly documented as out of scope in the spec (plan.md §0.5)."),
    ("source_lineage", ROOT / "design/unikraft-virtio-gpu-spec-v1.md", ["Lineage transparency", "linux-version/linux-6.18", "qemu-version/qemu-11.0", "venus-protocol", "Unikraft"], "Spec records source lineage and non-reimplementation boundaries."),
    ("paper_truthful_blocker", ROOT / "paper/sections/08-evaluation.typ", ["modern PCI", "0x1050", "BLOCKED"], "Paper evaluation reports current QEMU/Unikraft blocker instead of overclaiming."),
]

FORBIDDEN = [
    ("forbidden_project_pci_driver", ROOT / "libs/libukvirtio_gpu", ["PCI_REGISTER_DRIVER", "struct pci_driver"], "Project must not fork Unikraft PCI discovery."),
]

def contains_all(path: Path, needles: list[str]) -> tuple[bool, list[str]]:
    if not path.exists():
        return False, ["missing file"]
    text = path.read_text(errors="replace") if path.is_file() else "\n".join(p.read_text(errors="replace") for p in path.rglob("*") if p.is_file())
    missing = [n for n in needles if n not in text]
    return not missing, missing

def main() -> int:
    rows=[]; ok=True
    for cid, path, needles, reason in CHECKS:
        passed, missing = contains_all(path, needles)
        rows.append({"id":cid, "status":"pass" if passed else "fail", "path":str(path.relative_to(ROOT)), "reason":reason, "missing":missing})
        ok &= passed
    for cid, path, needles, reason in FORBIDDEN:
        if not path.exists():
            passed=True; detail="absent"
        else:
            if needles:
                text="\n".join(p.read_text(errors="replace") for p in path.rglob("*") if p.is_file()) if path.is_dir() else path.read_text(errors="replace")
                found=[n for n in needles if n in text]
                passed=not found; detail=f"found={found}"
            else:
                passed=False; detail="exists"
        rows.append({"id":cid, "status":"pass" if passed else "fail", "path":str(path.relative_to(ROOT)) if path.exists() else str(path.relative_to(ROOT)), "reason":reason, "detail":detail})
        ok &= passed
    OUT.mkdir(parents=True, exist_ok=True)
    payload={"status":"pass" if ok else "fail", "written_at":time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "rows":rows}
    (OUT/"unikraft_alignment_latest.json").write_text(json.dumps(payload, indent=2)+"\n")
    md=["# Unikraft alignment gate", "", f"Status: `{payload['status']}`", "", "| Check | Status | Evidence |", "|---|---|---|"]
    for r in rows:
        md.append(f"| `{r['id']}` | `{r['status']}` | {r.get('reason','')} (`{r['path']}`) |")
    (OUT/"unikraft_alignment_latest.md").write_text("\n".join(md)+"\n")
    print(f"unikraft_alignment_check: {payload['status']}")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
