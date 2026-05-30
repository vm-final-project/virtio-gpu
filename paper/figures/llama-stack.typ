#import "@preview/fletcher:0.5.8" as fletcher: diagram, edge, node

#let ink    = luma(25)
#let pfill  = rgb("edf3ed")
#let sfill  = rgb("f6f2ea")
#let bfill  = rgb("fafafa")
#let solid  = 0.8pt + ink
#let thin   = 0.65pt + ink
#let dashed = (paint: ink, thickness: 0.65pt, dash: "dashed")

#let lbl(a, b) = stack(
  spacing: 0.4em,
  align(center)[#text(weight: "bold", size: 6.8pt)[#a]],
  align(center)[#text(size: 6pt)[#b]],
)
#let bnd_node = node.with(fill: "none", stroke: 1.1pt + ink, inset: 6pt, corner-radius: 3pt)
#let bnd_title(t) = stack(align(left + top)[#text(weight: "bold", size: 8pt)[#t]])

#figure(
  scale(x: 90%, y: 90%, reflow: true)[
  #diagram(
    spacing: (0.8em, 0.65em),
    cell-size: (22mm, 10mm),
    edge-stroke: 0.75pt + ink,
    mark-scale: 63%,

    // ── LLAMA0 gate ──────────────────────────────────────────────────────
    node((0,0),
      lbl([LLAMA0 — app-llama (15+55 checks PASS)],
          [libukmodel: read-only GGUF-v3 view, FNV-1a checksum]),
      width: 58mm, fill: pfill, stroke: solid, name: <l0a>),
    node((0,1),
      lbl([libukggml — GGUF header parser + synthetic decode],
          [bounds-checked cursor · depth-limited value skip · deterministic SplitMix32 token gen]),
      width: 58mm, fill: pfill, stroke: solid, name: <l0b>),
    node((0,2),
      lbl([libukvenus connection path (documented)],
          [ggml-vulkan backend: SUBMIT\_3D tensor ops → host GPU — path ready, awaits kraft rebuild]),
      width: 58mm, fill: bfill, stroke: dashed, name: <l0c>),

    // ── LLAMA1 gate ──────────────────────────────────────────────────────
    node((0,3),
      lbl([LLAMA1 — app-llama-bench (39 checks PASS)],
          [libukllama: scalar matmul + layer benchmark (C wrapper around ggml compute API)]),
      width: 58mm, fill: pfill, stroke: solid, name: <l1a>),
    node((0,4),
      lbl([ggml\_compute\_bench — real upstream libggml\*.a linked],
          [ggml\_backend\_cpu\_init() + ggml\_backend\_graph\_compute(); 5 matrix sizes]),
      width: 58mm, fill: pfill, stroke: solid, name: <l1b>),
    node((0,5),
      lbl([Peak: ~27.7 GFLOPS (1T, 2048×1024×2048 matmul)],
          [Layer bench: attn\_proj + ffn\_up + ffn\_down; ~77 GFLOPS (batched)]),
      width: 58mm, fill: pfill, stroke: solid, name: <l1c>),

    // ── LLAMA2 gate ──────────────────────────────────────────────────────
    node((0,6),
      lbl([LLAMA2 — app-llama-full (llama\_full\_test PASS)],
          [Links real libllama.a + libggml\*.a; full transformer forward pass with synthetic weights]),
      width: 58mm, fill: pfill, stroke: solid, name: <l2a>),
    node((0,7),
      lbl([RMSNorm + QKV + multi-head attention + SwiGLU FFN],
          [~16 GFLOPS · ~41 k tok/s equivalent · determinism check · 3 reps PASS]),
      width: 58mm, fill: pfill, stroke: solid, name: <l2b>),
    node((0,8),
      lbl([Full token-streaming inference — blocked],
          [requires GGUF model file via 9pfs/initramfs (not yet wired into Kraftfile) + pthreads]),
      width: 58mm, fill: bfill, stroke: dashed, name: <l2c>),

    // ── GPU acceleration path (future) ───────────────────────────────────
    node((0,9),
      lbl([GPU acceleration — ggml-vulkan over Mesa Venus path (future)],
          [SUBMIT\_3D tensor descriptors via libukdrm\_virtgpu (G5) → host GPU; blocked:no-compute-payload]),
      width: 58mm, fill: bfill, stroke: dashed, name: <lgpu>),

    // ── Edges: main chain ─────────────────────────────────────────────────
    edge(<l0a>, <l0b>, "->"),
    edge(<l0b>, <l0c>, "-->", stroke: dashed),
    edge(<l0b>, <l1a>, "->"),
    edge(<l1a>, <l1b>, "->"),
    edge(<l1b>, <l1c>, "->"),
    edge(<l1c>, <l2a>, "->"),
    edge(<l2a>, <l2b>, "->"),
    edge(<l2b>, <l2c>, "-->", stroke: dashed),
    edge(<l2b>, <lgpu>, "-->", stroke: dashed),

    // ── Boundary boxes ────────────────────────────────────────────────────
    bnd_node(
      enclose: (<l0a>, <l0b>, <l0c>),
      fill: rgb("f4f8f4"), name: <b0>,
      bnd_title([LLAMA0: substrate parse + decode (PASS)]),
    ),
    bnd_node(
      enclose: (<l1a>, <l1b>, <l1c>),
      fill: rgb("f4f8f0"), name: <b1>,
      bnd_title([LLAMA1: real ggml CPU arithmetic (PASS)]),
    ),
    bnd_node(
      enclose: (<l2a>, <l2b>, <l2c>),
      fill: rgb("f0f8f0"), name: <b2>,
      bnd_title([LLAMA2: full transformer forward pass (PASS-substrate)]),
    ),
    bnd_node(
      enclose: (<lgpu>,),
      fill: rgb("fafafa"), name: <bgpu>,
      bnd_title([GPU acceleration (future / blocked)]),
    ),
  )
  ],
  caption: [
    LLAMA compute substrate stack. Three evidence gates build progressively. *LLAMA0* (15+55 checks PASS) validates the Unikraft GGUF parsing and synthetic decode substrate. *LLAMA1* (39 checks PASS) links real upstream `libggml\*.a` and measures actual CPU arithmetic throughput via `ggml_backend_cpu_init()`: ~27.7 GFLOPS peak (single thread, 2048×1024×2048 matmul). *LLAMA2* (PASS-substrate) links `libllama.a` and runs a full multi-head attention + SwiGLU FFN forward pass with synthetic weights (~16 GFLOPS, ~41 k tok/s equivalent). Full token-streaming inference with a real GGUF model file is blocked by model delivery (9pfs/initramfs not wired into Kraftfile). GPU acceleration via ggml-vulkan over Mesa Venus is the documented future path (the VirtIO-GPU transport is ready; guest-side compute payload encoding is not yet implemented).
  ],
) <fig:llama-stack>
