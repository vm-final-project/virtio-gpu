#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#outline-slide()

= System Design

== Key Principle: Single-Purpose, Single-Client GPU Driver

*Unikraft = one process, one purpose → GPU has exactly one client*

Replace DRM multiplexing with a *minimal exclusive-client driver*

#table(
  columns: (1fr, 1.3fr, 1.2fr),
  inset: 7pt,
  stroke: 0.5pt,
  align: (left, center, center),
  table.header([Dimension], [Linux virtio-gpu], [VOGUE]),
  [Graphics stack], [~3M LoC], [~5K LoC],
  [Address space], [Kernel + User], [Single],
  [GPU clients], [Multiple (needs arbiter)], [Exclusive single client],
  [Command path], [app → ioctl → kernel → virtio], [app → direct virtio ring],
)

*Unlocks:*
- No DRM multiplexing → ~1,650 LoC driver
- No user/kernel boundary → zero ioctl overhead
- Direct VirtIO ring access

#metadata((
  t: "Note",
  v: "【初始】這頁是整個設計的核心原則：Unikraft 只有一個 process，所以 GPU 設備只有一個客戶端。這個觀察讓我們可以完全跳過 DRM 的多工仲裁邏輯。表格量化了這個選擇的影響：圖形棧從三百萬行縮小到五千行，命令路徑從 app→ioctl→kernel→virtio 縮短為直接存取 virtio ring。【點擊後】這個設計解鎖了三個好處：驅動邏輯極度精簡、沒有 ioctl 邊界的 overhead、以及低延遲的直接 ring 存取。",
)) <pdfpc>

== VOGUE System Architecture

#table(
  columns: (auto, 1fr),
  inset: 7pt,
  stroke: 0.5pt,
  align: (center + horizon, left + horizon),
  table.header([Layer], [Components]),
  [Application], [kmscube / glmark2 / llama.cpp (*unmodified*)],
  [Compat shim], [`libukegl` (EGL/GLES2/GBM shim) + `libukswrender` (264 LoC)],
  [Venus / Vulkan], [`libukvenus` (Vulkan→Venus) + `libukggml_vk`],
  [VirtIO frontend], [`libukvirtio_gpu` + `libukdma`],
  [Transport], [Unikraft PCI / virtio (*reused*)],
)

- Host: `QEMU virtio-gpu-gl-pci` → `virglrenderer` → Host Vulkan / GL driver
- Guest image size: *292 KB* (vs Linux kernel 9.2 MB — 31× smaller)
- Boot time: *10–11 ms* (vs Linux + initramfs 857–861 ms — 80× faster)

#metadata((
  t: "Note",
  v: "這張表展示 VOGUE 的完整分層架構。最底層的 Transport 直接重用 Unikraft 既有的 PCI 和 virtio 基礎設施，完全不修改。往上是 VirtIO 前端、Venus 協議層、相容層、應用層，每一層都有明確的職責。這個架構帶來的效果非常顯著：guest image 只有 292 KB，是 Linux kernel 的三十分之一；開機時間只要 10 毫秒，是 Linux 的八十分之一。",
)) <pdfpc>

== VOGUE vs Linux: Layer by Layer

#let lbox(title, sub) = block(
  fill: luma(238),
  stroke: 0.4pt,
  radius: 2pt,
  inset: (x: 6pt, y: 4pt),
  width: 100%,
)[*#title*#if sub != "" [\ #text(size: 0.65em, fill: luma(85), sub)]]

#let vbox(title, sub) = block(
  fill: rgb("#fff9c4"),
  stroke: 0.5pt,
  radius: 2pt,
  inset: (x: 6pt, y: 4pt),
  width: 100%,
)[*#title*#if sub != "" [\ #text(size: 0.65em, fill: luma(85), sub)]]

#let ll(n, name) = align(right, text(size: 0.62em, fill: luma(120))[L#n #name])

#grid(
  columns: (4.5em, 20em, 1.4em, 20em),
  gutter: 8pt,
  align: (right + horizon, left, center + horizon, left),
  [], text(weight: "bold")[Linux VM], [], text(weight: "bold")[VOGUE Unikraft],
  ll(1, "App"), lbox("app", "kmscube / llama.cpp"), [≡], vbox("app", "unmodified upstream"),
  ll(2, "Shim"),
  lbox("libEGL + libgbm + libdrm", "real Linux platform APIs"),
  [→],
  vbox("libukegl + libukswrender", "264 LoC shim"),

  ll(3, "Venus"),
  lbox("Vulkan loader + Mesa Venus", "dynamic ICD, Vulkan→Venus"),
  [→],
  vbox("libukggml_vk + libukvenus", "static dispatch + same encoding"),

  ll(4, "VirtIO"),
  lbox("Linux virtio-gpu driver", "/dev/dri ioctl → kernel"),
  [→],
  vbox("libukvirtio_gpu", "direct virtqueue, no ioctl"),

  ll(5, "Trans"),
  lbox("kernel virtio / PCI", "reused unchanged"),
  [≡],
  vbox("Unikraft PCI / virtio", "reused unchanged"),
)

#v(0.3em)

#align(center)[#block(fill: rgb("#d4edda"), stroke: 0.4pt, radius: 2pt, inset: (x: 10pt, y: 6pt), width: 82%)[
    *QEMU virtio-gpu* → *virglrenderer* → *Host GPU driver* #h(2em) _(shared)_]
]

#text(size: 0.75em)[≡ same · → replaced with leaner VOGUE equivalent]

#metadata((
  t: "Note",
  v: "這張圖把五層架構和 Linux 的對應一起展示。L1（App）完全不動。L2（Shim）：Linux 用真實的 libEGL/libgbm/libdrm，VOGUE 改用 264 行的 shim 來模擬這些 API，讓 app 誤以為自己在 Linux 上。L3（Venus/Vulkan）：Linux 用 Vulkan loader 動態載入 ICD，VOGUE 改用靜態 dispatch table（libukggml_vk）；Venus 編碼本身邏輯等價（libukvenus ≡ Mesa Venus driver）。L4（VirtIO frontend）：Linux 透過 /dev/dri ioctl 進 kernel，VOGUE 直接操作 virtqueue，省掉 ioctl overhead。L5（Transport）：Unikraft PCI/virtio 直接重用，不修改。Host 端（QEMU、virglrenderer、host GPU driver）完全不動，這就是為什麼 VOGUE 能跟現有 hypervisor 直接相容。",
)) <pdfpc>

== Design Goals

- *DG1* Standards-based: follow the OASIS VirtIO-GPU spec, compatible with QEMU / crosvm / cloud-hypervisor
- *DG2* Small trusted substrate: guest graphics stack ~5,000 LoC, fully auditable
- *DG3* Application source compatibility: kmscube / llama.cpp upstream sources *unmodified*
- *DG4* Progressive validation: bottom-up — native tests → SW render → ABI → QEMU transport → accelerated render

*DG5 — Evidence-Gated Claims*

- `gfx.kmscube.sw` PASS *≠* virgl / GPU acceleration
- `proto.real-driver` PASS *≠* QEMU execution
- Host Linux baseline *≠* Unikraft runtime

*27 rows: 18 PASS, 9 blocked (cause + unblock documented)*

#metadata((
  t: "Note",
  v: "【初始】設計目標 DG1 到 DG4 比較標準：使用官方 VirtIO 標準、保持程式碼小且可審計、不修改上游原始碼、漸進式驗證策略。【點擊後】DG5 是方法論上最重要的貢獻。我們建立了嚴格的證據閘門制度，明確區分不同層次的宣告。27 個 evidence row，18 個通過、9 個有明確記錄的阻塞原因。blocked 不代表失敗，而是代表已知有哪些阻塞、怎麼解決。",
)) <pdfpc>
