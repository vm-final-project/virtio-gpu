#!/usr/bin/env python3
"""Generate the VirtIO-GPU cross-boundary dependency graph.

Implements docs/dependency-graph-plan.md. Extracts a directed multigraph of
virtio-gpu dependencies across three codebases + one protocol seam:

  GUEST   - Linux  : ../linux/drivers/gpu/drm/virtio   (DRM/KMS/GEM tower)
  GUEST   - VOGUE  : virtio-gpu/libs/*                 (libuk* static dispatch)
  SEAM            : VirtIO-GPU control/cursor queues + Venus ring (the contract)
  HYPERV  - QEMU   : ../qemu-src/hw/display/virtio-gpu-*.c + virglrenderer

Every edge carries a `kind`:
  build   - wired at build time (Kconfig select / Makefile.uk / meson source_set)
  compile - #include reachability
  runtime - virtqueue / ring command flow at execution time (curated + cited)

Outputs (under results/depgraph/):
  edges.json            normalized edge dataset (D1)
  linux.dot vogue.dot qemu.dot          per-repo graphs (D2)
  view-a-collapse.{dot,mmd}             Linux tower vs VOGUE dispatch diff (D3)
  view-b-edge-kinds.{dot,mmd}           build/compile/runtime overlay (D3)
  view-c-spine.{dot,mmd}                shared protocol opcode map (D3)
  *.svg                                 rendered if graphviz `dot` is present

Default mode parses the real source files (no toolchain needed). Pass --precise
to additionally resolve compile edges with `clang -M` (not required for the
subsystem-level views).

Usage:  python3 scripts/gen_depgraph.py [--out DIR] [--check]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent          # virtio-gpu/
WORKSPACE = REPO.parent                                 # /mydata/JerryT
LINUX_DRV = WORKSPACE / "linux" / "drivers" / "gpu" / "drm" / "virtio"
QEMU_DISPLAY = WORKSPACE / "qemu-src" / "hw" / "display"
LIBS = REPO / "libs"

# --- edge model -----------------------------------------------------------

class Graph:
    def __init__(self) -> None:
        self.nodes: dict[str, dict] = {}
        self.edges: list[dict] = []

    def node(self, nid: str, label: str | None = None, repo: str = "",
             role: str = "") -> str:
        n = self.nodes.setdefault(nid, {"id": nid, "label": label or nid,
                                        "repo": repo, "role": role})
        if label:
            n["label"] = label
        if repo:
            n["repo"] = repo
        if role:
            n["role"] = role
        return nid

    def edge(self, src: str, dst: str, kind: str, repo: str,
             label: str = "") -> None:
        if src == dst:  # drop self-includes / intra-node loops
            return
        self.edges.append({"src": src, "dst": dst, "kind": kind,
                           "repo": repo, "label": label})


# --- VOGUE (Unikraft) extraction ------------------------------------------

# LIBUK* Kconfig symbol -> lib directory name, discovered from libs/*/Config.uk.
def _vogue_symbol_map() -> dict[str, str]:
    sym2dir: dict[str, str] = {}
    for cfg in sorted(LIBS.glob("*/Config.uk")):
        d = cfg.parent.name
        for m in re.finditer(r"^\s*config\s+(\w+)", cfg.read_text(), re.M):
            sym2dir[m.group(1)] = d
    return sym2dir


def extract_vogue(g: Graph) -> None:
    sym2dir = _vogue_symbol_map()
    for cfg in sorted(LIBS.glob("*/Config.uk")):
        d = cfg.parent.name
        g.node(f"vogue:{d}", d, "vogue", "lib")
        text = cfg.read_text()
        # build edges: `select FOO` / `depends on FOO`
        for m in re.finditer(r"^\s*(?:select|depends on)\s+(\w+)", text, re.M):
            sym = m.group(1)
            tgt = sym2dir.get(sym)
            if tgt and tgt != d:  # skip intra-lib sub-symbol self-deps
                g.edge(f"vogue:{d}", f"vogue:{tgt}", "build", "vogue", "select")
            else:  # external Unikraft core symbol
                g.node(f"vogue:ext:{sym}", sym, "vogue", "ext")
                g.edge(f"vogue:{d}", f"vogue:ext:{sym}", "build", "vogue",
                       "select")
    # compile edges: cross-lib `#include <uk/...>`
    hdr2dir = {
        "uk/virtio_gpu.h": "libukvirtio_gpu", "uk/venus.h": "libukvenus",
        "uk/drm_virtgpu.h": "libukvirtgpu_drm", "uk/swrender.h": "libukswrender",
        "uk/drm_compat.h": "libukdrm_compat", "uk/gbm_compat.h": "libukgbm_compat",
        "uk/ggml_vulkan.h": "libukggml_vk",
    }
    for d in sorted(p.name for p in LIBS.iterdir() if p.is_dir()):
        seen: set[str] = set()
        for src in (LIBS / d).rglob("*"):
            if src.suffix not in (".c", ".h", ".cpp", ".cc"):
                continue
            for m in re.finditer(r'#include\s+[<"]([^">]+)[">]', src.read_text(errors="ignore")):
                tgt = hdr2dir.get(m.group(1))
                if tgt and tgt != d and tgt not in seen:
                    seen.add(tgt)
                    g.edge(f"vogue:{d}", f"vogue:{tgt}", "compile", "vogue",
                           m.group(1))


# --- Linux extraction ------------------------------------------------------

def extract_linux(g: Graph) -> None:
    if not LINUX_DRV.is_dir():
        print(f"warn: {LINUX_DRV} missing; skipping linux", file=sys.stderr)
        return
    g.node("linux:virtio-gpu.ko", "virtio-gpu.ko", "linux", "module")
    # build: Makefile object membership + Kconfig selects
    mk = (LINUX_DRV / "Makefile").read_text()
    objs = re.findall(r"(\w+)\.o", mk.split(":=", 1)[1] if ":=" in mk else mk)
    for o in objs:
        if o == "virtio-gpu":
            continue
        g.node(f"linux:{o}", o, "linux", "obj")
        g.edge(f"linux:{o}", "linux:virtio-gpu.ko", "build", "linux", "link")
    kc = (LINUX_DRV / "Kconfig").read_text()
    for sym in re.findall(r"^\s*select\s+(\w+)", kc, re.M):
        g.node(f"linux:sub:{sym}", sym, "linux", "subsystem")
        g.edge("linux:virtio-gpu.ko", f"linux:sub:{sym}", "build", "linux",
               "select")
    # compile: in-tree .c -> local .h hub + key external subsystem headers
    SUB = {"drm/drm_drv.h": "DRM", "drm/drm_gem_shmem_helper.h": "DRM_GEM_SHMEM_HELPER",
           "drm/drm_atomic_helper.h": "DRM_KMS_HELPER", "linux/virtio.h": "VIRTIO",
           "linux/virtio_dma_buf.h": "VIRTIO_DMA_SHARED_BUFFER",
           "linux/virtio_ring.h": "VIRTIO"}
    for src in sorted(LINUX_DRV.glob("*.c")):
        o = src.stem
        nid = f"linux:{o}"
        g.node(nid, o, "linux", "obj")
        for m in re.finditer(r'#include\s+[<"]([^">]+)[">]', src.read_text()):
            h = m.group(1)
            if h.startswith("virtgpu_") and h.endswith(".h"):
                g.edge(nid, "linux:virtgpu_drv", "compile", "linux", h)
            elif h in SUB:
                g.node(f"linux:sub:{SUB[h]}", SUB[h], "linux", "subsystem")
                g.edge(nid, f"linux:sub:{SUB[h]}", "compile", "linux", h)


# --- QEMU extraction -------------------------------------------------------

def extract_qemu(g: Graph) -> None:
    if not QEMU_DISPLAY.is_dir():
        print(f"warn: {QEMU_DISPLAY} missing; skipping qemu", file=sys.stderr)
        return
    EXT = {"virglrenderer.h": "virglrenderer",
           "rutabaga_gfx/rutabaga_gfx_ffi.h": "rutabaga"}
    for src in sorted(QEMU_DISPLAY.glob("virtio-gpu*.c")):
        o = src.stem
        nid = f"qemu:{o}"
        g.node(nid, o, "qemu", "obj")
        for m in re.finditer(r'#include\s+[<"]([^">]+)[">]', src.read_text()):
            h = m.group(1)
            base = h.split("/")[-1]
            if h in EXT or base in ("rutabaga_gfx_ffi.h",):
                lib = EXT.get(h, "rutabaga")
                g.node(f"qemu:ext:{lib}", lib, "qemu", "ext")
                g.edge(nid, f"qemu:ext:{lib}", "compile", "qemu", base)
            elif base == "virtio-gpu.h":
                g.edge(nid, "qemu:virtio-gpu-base", "compile", "qemu", base)
            elif base == "virtio.h":
                g.node("qemu:ext:virtio-core", "virtio-core", "qemu", "ext")
                g.edge(nid, "qemu:ext:virtio-core", "compile", "qemu", base)
    # build: meson.build source_set feature guards
    meson = (QEMU_DISPLAY / "meson.build").read_text()
    for feat, files in re.findall(
            r"_ss\.add\(when:\s*\[([^\]]*)\][^)]*files\(([^)]*)\)", meson):
        guard = "+".join(re.findall(r"'CONFIG_(\w+)'|(\b\w+)\b", feat) and
                         re.findall(r"(virgl|opengl|rutabaga|CONFIG_VIRTIO_PCI)", feat))
        for fn in re.findall(r"'([^']+\.c)'", files):
            o = Path(fn).stem
            if not o.startswith("virtio"):
                continue
            g.node(f"qemu:{o}", o, "qemu", "obj")
            if guard:
                g.node(f"qemu:feat:{guard}", guard, "qemu", "feature")
                g.edge(f"qemu:{o}", f"qemu:feat:{guard}", "build", "qemu",
                       "gated-by")


# --- runtime edges (curated, cited) ---------------------------------------
# Not derivable from #include; each row cites the source site that justifies it.
RUNTIME_EDGES = [
    # (src, dst, opcode/label, citation)
    ("vogue:libukggml_vk", "vogue:libukvenus", "vk cmd encode",
     "libukggml_vk static dispatch -> libukvenus encoder"),
    ("vogue:libukvenus", "vogue:libukvirtio_gpu", "VENUS ring submit",
     "libukvenus ring -> virtqueue kick"),
    ("vogue:libukvirtgpu_drm", "vogue:libukvirtio_gpu", "EXECBUFFER ioctl",
     "drm shim -> virtio_gpu frontend"),
    ("vogue:libukvirtio_gpu", "seam:ctrlq", "VIRTIO_GPU_CMD_*",
     "control queue add_buf/kick"),
    ("vogue:libukvenus", "seam:venus-ring", "VK command stream",
     "Venus ring shared buffer"),
    ("linux:virtgpu_vq", "seam:ctrlq", "VIRTIO_GPU_CMD_*",
     "virtgpu_vq.c virtqueue_add/kick"),
    ("linux:virtgpu_submit", "seam:venus-ring", "EXECBUFFER",
     "virtgpu_submit.c context submit"),
    ("seam:ctrlq", "qemu:virtio-gpu", "cmd dispatch",
     "virtio-gpu.c virtio_gpu_handle_ctrl"),
    ("seam:venus-ring", "qemu:virtio-gpu-virgl", "venus decode",
     "virtio-gpu-virgl.c -> virglrenderer venus"),
    ("qemu:virtio-gpu-virgl", "qemu:ext:virglrenderer", "host Vulkan",
     "virgl_renderer_* host execution"),
]

SEAM_NODES = [
    ("seam:ctrlq", "VirtIO-GPU control/cursor queues"),
    ("seam:venus-ring", "Venus command ring"),
]


def add_runtime(g: Graph) -> None:
    for nid, label in SEAM_NODES:
        g.node(nid, label, "seam", "protocol")
    for src, dst, label, _cite in RUNTIME_EDGES:
        # tolerate skipped repos
        if src in g.nodes and dst.startswith("seam:"):
            g.node(dst, dict(SEAM_NODES).get(dst, dst), "seam", "protocol")
        if src in g.nodes or src.startswith("seam:"):
            g.node(src, g.nodes.get(src, {}).get("label", src),
                   g.nodes.get(src, {}).get("repo", "seam"))
            g.node(dst, g.nodes.get(dst, {}).get("label", dst),
                   g.nodes.get(dst, {}).get("repo", "seam"))
            g.edge(src, dst, "runtime", "seam", label)


# --- DOT / Mermaid emission -----------------------------------------------

KIND_STYLE = {"build": 'style=dashed,color="#888888"',
              "compile": 'color="#3b6fb6"',
              "runtime": 'penwidth=2.2,color="#c0392b"'}
REPO_FILL = {"linux": "#fde2e2", "vogue": "#e2f0fd", "qemu": "#e8f6e8",
             "seam": "#fff3c4"}


def _dot_node(n: dict) -> str:
    fill = REPO_FILL.get(n["repo"], "#eeeeee")
    shape = "box"
    if n.get("role") == "protocol":
        shape = "hexagon"
    elif n.get("role") in ("subsystem", "ext", "feature"):
        shape = "ellipse"
    return (f'  "{n["id"]}" [label="{n["label"]}",shape={shape},'
            f'style=filled,fillcolor="{fill}"];')


def write_dot(path: Path, g: Graph, edges: list[dict], title: str,
              clusters: bool = True) -> None:
    used = {e["src"] for e in edges} | {e["dst"] for e in edges}
    lines = [f'digraph "{title}" {{', '  rankdir=LR;', '  node [fontsize=10];',
             f'  label="{title}"; labelloc=t; fontsize=14;']
    if clusters:
        for repo, name in (("linux", "Linux DRM guest"), ("vogue", "VOGUE (Unikraft) guest"),
                           ("seam", "Protocol seam"), ("qemu", "QEMU hypervisor")):
            members = [n for nid, n in g.nodes.items()
                       if nid in used and n["repo"] == repo]
            if not members:
                continue
            lines.append(f'  subgraph "cluster_{repo}" {{ label="{name}"; '
                         f'style=rounded; color="#bbbbbb";')
            lines += ["  " + _dot_node(n) for n in members]
            lines.append("  }")
    else:
        lines += [_dot_node(g.nodes[nid]) for nid in used if nid in g.nodes]
    for e in edges:
        st = KIND_STYLE[e["kind"]]
        lbl = f',label="{e["label"]}"' if e["label"] else ""
        lines.append(f'  "{e["src"]}" -> "{e["dst"]}" [{st}{lbl}];')
    lines.append("}")
    path.write_text("\n".join(lines) + "\n")


def write_mermaid(path: Path, g: Graph, edges: list[dict], title: str) -> None:
    arrow = {"build": "-.->", "compile": "-->", "runtime": "==>"}
    safe = lambda s: re.sub(r"\W", "_", s)
    used = {e["src"] for e in edges} | {e["dst"] for e in edges}
    lines = [f"%% {title}", "flowchart LR"]
    for repo, name in (("linux", "Linux DRM guest"), ("vogue", "VOGUE guest"),
                       ("seam", "Protocol seam"), ("qemu", "QEMU host")):
        members = [nid for nid in used
                   if g.nodes.get(nid, {}).get("repo") == repo]
        if not members:
            continue
        lines.append(f'  subgraph {repo}["{name}"]')
        for nid in members:
            lines.append(f'    {safe(nid)}["{g.nodes[nid]["label"]}"]')
        lines.append("  end")
    for e in edges:
        lbl = f'|{e["label"]}|' if e["label"] else ""
        lines.append(f'  {safe(e["src"])} {arrow[e["kind"]]}{lbl} {safe(e["dst"])}')
    path.write_text("\n".join(lines) + "\n")


def render_svg(out: Path) -> None:
    dot = shutil.which("dot")
    if not dot:
        print("note: graphviz `dot` not found; wrote .dot/.mmd only "
              "(install graphviz to render .svg)", file=sys.stderr)
        return
    for f in sorted(out.glob("*.dot")):
        svg = f.with_suffix(".svg")
        subprocess.run([dot, "-Tsvg", str(f), "-o", str(svg)], check=True)
    print(f"rendered svg via {dot}")


# --- views -----------------------------------------------------------------

def build_views(g: Graph, out: Path) -> dict:
    e = g.edges
    # View A: collapse - Linux build/compile subgraph vs VOGUE, both -> seam
    a = [x for x in e if (x["repo"] in ("linux", "vogue") and x["kind"] != "runtime")]
    a += [x for x in e if x["dst"].startswith("seam:") and x["kind"] == "runtime"]
    write_dot(out / "view-a-collapse.dot", g, a, "View A: The Collapse (Linux tower vs VOGUE dispatch)")
    write_mermaid(out / "view-a-collapse.mmd", g, a, "View A: The Collapse")
    # View B: edge-kind overlay on the VOGUE Vulkan hot path + seam
    hot = {"vogue:libukggml_vk", "vogue:libukvenus", "vogue:libukvirtio_gpu",
           "vogue:libukvirtgpu_drm", "vogue:libukvk_icd", "seam:ctrlq", "seam:venus-ring"}
    b = [x for x in e if x["src"] in hot and (x["dst"] in hot or x["dst"].startswith("seam:"))]
    write_dot(out / "view-b-edge-kinds.dot", g, b, "View B: Three Kinds of One Edge")
    write_mermaid(out / "view-b-edge-kinds.mmd", g, b, "View B: Edge Kinds")
    # View C: invariant spine - runtime opcode flow across all three columns
    c = [x for x in e if x["kind"] == "runtime"]
    write_dot(out / "view-c-spine.dot", g, c, "View C: The Invariant Protocol Spine")
    write_mermaid(out / "view-c-spine.mmd", g, c, "View C: Protocol Spine")
    return {"view_a_edges": len(a), "view_b_edges": len(b), "view_c_edges": len(c)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(REPO / "results" / "depgraph"))
    ap.add_argument("--check", action="store_true",
                    help="fail if edges.json drifts from committed copy")
    ap.parse_args()
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    g = Graph()
    extract_vogue(g)
    extract_linux(g)
    extract_qemu(g)
    add_runtime(g)

    # provenance: record sibling commits where available
    def _commit(p: Path) -> str:
        try:
            return subprocess.check_output(["git", "-C", str(p), "rev-parse",
                                            "--short", "HEAD"],
                                           stderr=subprocess.DEVNULL).decode().strip()
        except Exception:
            return "unknown"

    dataset = {
        "schema_version": 1,
        "provenance": {"linux": _commit(WORKSPACE / "linux"),
                       "qemu": _commit(WORKSPACE / "qemu-src"),
                       "vogue": _commit(REPO)},
        "counts": {"nodes": len(g.nodes), "edges": len(g.edges),
                   "build": sum(1 for x in g.edges if x["kind"] == "build"),
                   "compile": sum(1 for x in g.edges if x["kind"] == "compile"),
                   "runtime": sum(1 for x in g.edges if x["kind"] == "runtime")},
        "runtime_citations": [{"src": s, "dst": d, "label": l, "cite": c}
                              for s, d, l, c in RUNTIME_EDGES],
        "nodes": list(g.nodes.values()),
        "edges": g.edges,
    }
    edges_path = out / "edges.json"
    new = json.dumps(dataset, indent=2, sort_keys=False)
    if args.check:
        old = edges_path.read_text() if edges_path.exists() else ""
        if old.strip() != new.strip():
            print("depgraph-check: edges.json is stale; run `make depgraph`",
                  file=sys.stderr)
            return 1
        print("depgraph-check: OK")
        return 0
    edges_path.write_text(new + "\n")

    # per-repo graphs
    for repo in ("linux", "vogue", "qemu"):
        re_edges = [x for x in g.edges if x["repo"] == repo]
        write_dot(out / f"{repo}.dot", g, re_edges, f"{repo} virtio-gpu deps",
                  clusters=False)
    vcounts = build_views(g, out)
    render_svg(out)

    print(f"wrote {edges_path} : {dataset['counts']}")
    print(f"views: {vcounts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
