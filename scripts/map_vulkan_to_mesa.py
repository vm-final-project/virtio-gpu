#!/usr/bin/env python3
"""
map_vulkan_to_mesa.py  (v2 — cscope-assisted)
───────────────────────────────────────────────
Maps each Vulkan API used by ggml-vulkan.cpp to its location in Mesa's
virtio/Venus driver, using cscope for precise function-definition lookup.

Architecture discovered:
  vkFooBar  (Vulkan C API, called by ggml via Vulkan-Hpp)
      │
      │  Mesa Venus driver names the public entry-point  vn_FooBar()
      │  — defined in src/virtio/vulkan/vn_*.c
      │  OR handled purely via the venus-protocol inline encoder
      │  (vn_call_vkFooBar / vn_async_vkFooBar) without a public entry-point
      │
      ▼
  Venus ring (vn_ring / vn_cs) encodes the call into the shared-memory ring
      │
      ▼
  QEMU virtio-gpu / host renderer executes on the real GPU

Usage:
    python3 scripts/map_vulkan_to_mesa.py [ggml.cpp [vk.xml [mesa_root]]]
"""
import os, re, sys, csv, subprocess, shutil
from collections import defaultdict

# ── paths ──────────────────────────────────────────────────────────────────
BASE        = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GGML_PATH   = os.path.join(BASE, "../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp")
XML_PATH    = os.path.join(BASE, "../mesa/src/vulkan/runtime/registry/vk.xml")
MESA_ROOT   = os.path.join(BASE, "../mesa")
VOGUE_DISP  = os.path.join(BASE, "libs/libvulkan/runtime/vk_dispatch.c")
DOCS_DIR    = os.path.join(BASE, "docs")

# ── Vulkan-Hpp abbreviated method overrides (same as find_used_vulkan_apis) ─
VK_HPP_OVERRIDES = [
    ("vkBeginCommandBuffer",  r"(?:->|\.)\s*buf",               "begin"),
    ("vkEndCommandBuffer",    r"(?:->|\.)\s*buf",               "end"),
    ("vkResetCommandBuffer",  r"(?:->|\.)\s*buf",               "reset"),
    ("vkResetCommandPool",    r"(?:compute_ctx|transfer_ctx)",  "reset"),
    ("vkQueueSubmit",         r"(?:->|\.)\s*queue",             "submit"),
    ("vkQueueWaitIdle",       r"(?:->|\.)\s*queue",             "waitIdle"),
    ("vkDeviceWaitIdle",      r"(?:->|\.)\s*(?:device|dev)",    "waitIdle"),
]

# ── tier / category metadata ───────────────────────────────────────────────
FILE_META = {
    "vn_ring.c":              (1, "Venus ring / transport layer"),
    "vn_cs.c":                (1, "Command-stream encoder"),
    "vn_renderer_virtgpu.c":  (1, "Renderer: virtgpu DRM ioctl"),
    "vn_renderer_internal.c": (1, "Renderer: shared internals"),
    "vn_renderer_util.c":     (1, "Renderer: shared utilities"),
    "vn_common.c":            (1, "Common utilities & init"),
    "vn_icd.c":               (1, "ICD entry-point table"),
    "vn_instance.c":          (2, "VkInstance lifecycle"),
    "vn_physical_device.c":   (2, "VkPhysicalDevice / capabilities"),
    "vn_device.c":            (2, "VkDevice lifecycle"),
    "vn_device_memory.c":     (2, "VkDeviceMemory / map / bind"),
    "vn_buffer.c":            (3, "VkBuffer objects"),
    "vn_pipeline.c":          (3, "VkPipeline / shader modules"),
    "vn_descriptor_set.c":    (3, "VkDescriptorSet / pool / layout"),
    "vn_command_buffer.c":    (4, "VkCommandBuffer recording"),
    "vn_queue.c":             (4, "VkQueue / sync primitives"),
    "vn_feedback.c":          (4, "Timeline semaphore feedback"),
    "vn_query_pool.c":        (4, "VkQueryPool"),
    "vn_image.c":             (5, "VkImage (not used by ggml compute)"),
    "vn_render_pass.c":       (5, "Render passes (not used)"),
    "vn_wsi.c":               (5, "WSI / swapchain (not used)"),
    "vn_android.c":           (5, "Android extensions (not used)"),
    "vn_acceleration_structure.c": (5, "Ray-tracing (not used)"),
    "vn_host_copy.c":         (5, "Host-copy extension"),
    # protocol-only (no public vn_ entry-point, encoded inline)
    "venus-protocol (inline)": (2, "Direct ring encode — no separate vn_*.c entry"),
}

# ── parse vk.xml ───────────────────────────────────────────────────────────
import xml.etree.ElementTree as ET

def parse_vk_xml(xml_path):
    root = ET.parse(xml_path).getroot()
    names = set()
    for cmd in root.findall(".//commands/command"):
        proto = cmd.find("proto")
        if proto is not None:
            n = proto.findtext("name")
            if n: names.add(n)
        else:
            n = cmd.get("name")
            if n: names.add(n)
    return sorted(names)

# ── scan ggml ──────────────────────────────────────────────────────────────
def _auto_patterns(cmd):
    pats = [r"\b" + re.escape(cmd) + r"\b"]
    if not cmd.startswith("vk"): return pats
    camel = cmd[2].lower() + cmd[3:]
    pats += [r"\bvk::" + re.escape(camel) + r"\s*\(", r"\.\s*" + re.escape(camel) + r"\s*\(", r"->\s*" + re.escape(camel) + r"\s*\("]
    if cmd.startswith("vkCmd"):
        s = cmd[5].lower() + cmd[6:]
        pats += [r"\.\s*" + re.escape(s) + r"\s*\(", r"->\s*" + re.escape(s) + r"\s*\("]
    if cmd in ("vkDestroyDevice","vkDestroyInstance"): pats.append(r"\.\s*destroy\s*\(")
    if cmd.startswith("vkGetPhysicalDevice"):
        rest = cmd[19].lower()+cmd[20:]; meth="get"+rest[0].upper()+rest[1:]
        pats.append(r"\.\s*"+re.escape(meth)+r"\s*\(")
    if cmd.startswith("vkGetBuffer"):
        rest = cmd[11].lower()+cmd[12:]; meth="get"+rest[0].upper()+rest[1:]
        pats.append(r"\.\s*"+re.escape(meth)+r"\s*\(")
    return pats

def scan_ggml(source_path, commands):
    with open(source_path, "r", encoding="utf-8") as fh: content = fh.read()
    content = re.sub(r"//[^\n]*","",content)
    content = re.sub(r"/\*.*?\*/","",content,flags=re.DOTALL)
    found = {}
    cmd_set = set(commands)
    for cmd in commands:
        for pat in _auto_patterns(cmd):
            n = len(re.findall(pat, content))
            if n: found[cmd] = found.get(cmd,0)+n
    for vk_cmd, ctx_pat, method in VK_HPP_OVERRIDES:
        if vk_cmd not in cmd_set: continue
        pat = re.compile(ctx_pat + r"(?:\s*->\s*|\s*\.\s*)"+re.escape(method)+r"\s*\(")
        n = len(pat.findall(content))
        if n: found[vk_cmd] = found.get(vk_cmd,0)+n
    return found

# ── cscope lookup ──────────────────────────────────────────────────────────
def build_cscope_db(mesa_root):
    """Build cscope DB covering src/virtio + src/vulkan."""
    files_txt = "/tmp/mesa_vk_cscope_files.txt"
    db_path   = "/tmp/mesa_vk_cscope.out"
    src_dirs  = [os.path.join(mesa_root, d) for d in ("src/virtio", "src/vulkan")]
    lines = []
    for d in src_dirs:
        for root_, _, fnames in os.walk(d):
            for fn in fnames:
                if fn.endswith((".c",".h")):
                    lines.append(os.path.join(root_,fn))
    with open(files_txt,"w") as f: f.write("\n".join(lines)+"\n")
    subprocess.run(["cscope","-b","-k","-i",files_txt,"-f",db_path],
                   cwd=mesa_root, check=True, capture_output=True)
    return db_path

def cscope_find_def(db_path, symbol, mesa_root):
    """
    Returns (relative_file, lineno) for the function DEFINITION of `symbol`,
    or None if not found.  Uses cscope -L -1 (find definition).
    """
    result = subprocess.run(
        ["cscope","-d","-f",db_path,"-L","-1",symbol],
        capture_output=True, text=True
    )
    # cscope output: <file> <function> <lineno> <text>
    for line in result.stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) >= 3:
            fpath, fn_col, lineno = parts[0], parts[1], parts[2]
            rel = os.path.relpath(fpath, mesa_root)
            return (rel, int(lineno))
    return None

def cscope_find_caller(db_path, symbol, mesa_root):
    """
    For APIs that have NO vn_FooBar entry-point (handled purely via
    vn_call_vkFooBar in the protocol header), find the call site.
    Uses cscope -L -3 (find callers).
    """
    call_sym = "vn_call_" + symbol       # e.g. vn_call_vkGetPhysicalDeviceFeatures
    result = subprocess.run(
        ["cscope","-d","-f",db_path,"-L","-0",call_sym],
        capture_output=True, text=True
    )
    hits = []
    for line in result.stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) >= 3:
            fpath, fn_col, lineno = parts[0], parts[1], parts[2]
            rel = os.path.relpath(fpath, mesa_root)
            # Only care about .c files under src/virtio/vulkan/
            if "virtio/vulkan" in rel and rel.endswith(".c"):
                hits.append((rel, int(lineno)))
    # Return the first .c hit
    return hits[0] if hits else None

# ── VOGUE porting status ───────────────────────────────────────────────────
def vogue_status(vk_cmd):
    if not os.path.exists(VOGUE_DISP): return "?"
    with open(VOGUE_DISP,"r") as f: content = f.read()
    if not re.search(r'\b'+re.escape(vk_cmd)+r'\b', content): return "✗ missing"
    # find context around first mention
    lines = content.split("\n")
    for i,ln in enumerate(lines):
        if vk_cmd in ln:
            ctx = "\n".join(lines[max(0,i-3):i+6])
            if re.search(r'\bNULL\b|stub|TODO', ctx, re.I): return "⚙ stub"
            return "✓ done"
    return "⚙ stub"

# ── main ───────────────────────────────────────────────────────────────────
def main():
    if len(sys.argv)>1: ggml  = sys.argv[1]
    else:               ggml  = GGML_PATH
    if len(sys.argv)>2: xml   = sys.argv[2]
    else:               xml   = XML_PATH
    if len(sys.argv)>3: mesa  = sys.argv[3]
    else:               mesa  = MESA_ROOT

    if not shutil.which("cscope"):
        sys.exit("ERROR: cscope not found in PATH — please install it.")

    os.makedirs(DOCS_DIR, exist_ok=True)

    print("Step 1/4  Parsing vk.xml …")
    all_cmds = parse_vk_xml(xml)

    print("Step 2/4  Scanning ggml-vulkan.cpp …")
    used = scan_ggml(ggml, all_cmds)
    print(f"          {len(used)} APIs used by ggml")

    print("Step 3/4  Building cscope DB (src/virtio + src/vulkan) …")
    db = build_cscope_db(mesa)
    print(f"          DB: {db}")

    print("Step 4/4  Resolving each API via cscope …\n")

    rows = []
    for vk_cmd in sorted(used):
        vn_sym  = "vn_" + vk_cmd[2:]          # vkFooBar → vn_FooBar
        # Try: direct definition of vn_FooBar
        hit = cscope_find_def(db, vn_sym, mesa)
        method = "entry-point"
        if hit is None:
            # Try: vn_call_vkFooBar call site (protocol-only path)
            hit = cscope_find_caller(db, vk_cmd, mesa)
            method = "protocol-inline"
        if hit:
            rel_path, lineno = hit
            basename = os.path.basename(rel_path)
        else:
            rel_path, lineno, basename = "(not found)", 0, "(not found)"
            method = "—"

        tier, category = FILE_META.get(basename, FILE_META.get("venus-protocol (inline)", (9,"unknown")))
        if basename == "(not found)": tier, category = 9, "not found in Mesa virtio"

        status = vogue_status(vk_cmd)
        rows.append({
            "vk_cmd":    vk_cmd,
            "vn_sym":    vn_sym,
            "calls":     used[vk_cmd],
            "mesa_file": rel_path,
            "line":      lineno if lineno else "—",
            "method":    method,
            "tier":      tier,
            "category":  category,
            "status":    status,
        })

    # ── console table ───────────────────────────────────────────────────────
    print(f"{'Vulkan C API':<45} {'Mesa location':<50} {'How':<16} {'Tier'} {'VOGUE'}")
    print("─"*130)
    prev_tier = None
    for r in sorted(rows, key=lambda x:(x["tier"], x["mesa_file"], x["vk_cmd"])):
        if r["tier"] != prev_tier:
            labels={1:"Transport & encoding",2:"Instance / device bootstrap",
                    3:"Resource management",4:"Command recording & submission",
                    5:"Optional / compute-unused",9:"Unknown"}
            print(f"\n  ── Tier {r['tier']}: {labels.get(r['tier'],'')} ──")
            prev_tier = r["tier"]
        loc = f"{r['mesa_file']}:{r['line']}" if r['line']!='—' else r['mesa_file']
        print(f"  {r['vk_cmd']:<43} {loc:<50} {r['method']:<16} {r['tier']}     {r['status']}")

    # ── per-file summary ────────────────────────────────────────────────────
    fstats = defaultdict(lambda:{"total":0,"done":0,"stub":0,"missing":0})
    for r in rows:
        bn = os.path.basename(r["mesa_file"])
        fstats[bn]["total"] += 1
        key = "done" if "done" in r["status"] else "stub" if "stub" in r["status"] else "missing"
        fstats[bn][key] += 1

    print(f"\n\n{'Mesa file':<38} {'APIs':>4}  {'✓done':>5} {'⚙stub':>5} {'✗miss':>5}  Tier  Category")
    print("─"*100)
    for fn, s in sorted(fstats.items(), key=lambda x:(FILE_META.get(x[0],(9,""))[0], -x[1]["total"])):
        tier,cat = FILE_META.get(fn,(9,"?"))
        print(f"  {fn:<36} {s['total']:>4}  {s['done']:>5} {s['stub']:>5} {s['missing']:>5}    {tier}  {cat}")

    # ── write outputs ───────────────────────────────────────────────────────
    csv_path = os.path.join(DOCS_DIR,"vulkan_porting_map.csv")
    with open(csv_path,"w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(
            sorted(rows,key=lambda x:(x["tier"],x["mesa_file"],x["vk_cmd"])))
    print(f"\nCSV      → {csv_path}")

    md_path = os.path.join(DOCS_DIR,"vulkan_porting_map.md")
    tier_labels={1:"Transport & encoding (must port first)",2:"Instance / device bootstrap",
                 3:"Resource management",4:"Command recording & submission",
                 5:"Optional / compute-unused"}
    with open(md_path,"w") as f:
        f.write("# Vulkan API → Mesa Venus Porting Map\n\n")
        f.write("> Generated by `scripts/map_vulkan_to_mesa.py` (cscope-assisted).\n\n")
        f.write("**Status**: ✓ done in VOGUE `libs/libvulkan/` · ⚙ stub/NULL · ✗ missing\n\n")
        f.write("**Method**: `entry-point` = Mesa has a public `vn_FooBar()` in `vn_*.c` · "
                "`protocol-inline` = encoded directly via `vn_call_vkFooBar()` (no separate entry-point)\n\n")
        prev_tier = None
        for r in sorted(rows, key=lambda x:(x["tier"],x["mesa_file"],x["vk_cmd"])):
            if r["tier"] != prev_tier:
                f.write(f"\n## Tier {r['tier']} — {tier_labels.get(r['tier'],'Other')}\n\n")
                f.write("| Vulkan API | Mesa file | Line | Method | ggml calls | VOGUE |\n")
                f.write("|---|---|---:|:---:|---:|:---:|\n")
                prev_tier = r["tier"]
            fn = os.path.basename(r["mesa_file"])
            loc_link = f"[{fn}](../{r['mesa_file']})" if r["line"]!="—" else fn
            f.write(f"| `{r['vk_cmd']}` | {loc_link} | {r['line']} | {r['method']} | {r['calls']} | {r['status']} |\n")

        f.write("\n\n## Per-file Summary\n\n")
        f.write("| Mesa file | APIs | ✓done | ⚙stub | ✗missing | Tier | Category |\n")
        f.write("|---|---:|---:|---:|---:|:---:|---|\n")
        for fn,s in sorted(fstats.items(),key=lambda x:(FILE_META.get(x[0],(9,""))[0],-x[1]["total"])):
            tier,cat=FILE_META.get(fn,(9,"?"))
            f.write(f"| `{fn}` | {s['total']} | {s['done']} | {s['stub']} | {s['missing']} | {tier} | {cat} |\n")
    print(f"Markdown → {md_path}")

if __name__=="__main__":
    main()
