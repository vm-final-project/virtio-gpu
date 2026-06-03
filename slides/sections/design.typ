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

#pause

*Unlocks:*
- No DRM multiplexing → ~1,650 LoC driver
- No user/kernel boundary → virtqueue writes are direct function calls, no ioctl

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

#pause

- Host: `QEMU virtio-gpu-gl-pci` → `virglrenderer` → Host Vulkan / GL driver
- Guest image size: *292 KB* (vs Linux kernel 9.2 MB — 31× smaller)
- Boot time: *10–11 ms* (vs Linux + initramfs 857–861 ms — 80× faster)

#metadata((
  t: "Note",
  v: "這張表是整個系統的地圖，每一層職責如下。\n L1（Application）：kmscube、glmark2、llama.cpp，上游原始碼完全不修改，直接 link 進 unikernel image。\n L2（Compat shim）：libukegl 提供 EGL/GLES2/GBM/DRM 的 API stub，讓 app 誤以為自己在 Linux 上；libukswrender 是 264 行的 CPU rasterizer，負責 2D 路徑的像素繪製。\n L3（Venus/Vulkan）：libukvenus 把 Vulkan API 呼叫序列化成 Venus binary stream（PACKED 格式）；libukggml_vk 提供靜態 Vulkan dispatch table 給 ggml，省掉 Vulkan loader 的動態載入。\n L4（VirtIO frontend）：libukvirtio_gpu 實作完整的 VirtIO-GPU 協議（1,650 LoC），把 2D display 命令和 Venus SUBMIT_3D 透過 virtqueue 送給 QEMU；libukdma 管理 DMA 記憶體配置。\n L5（Transport）：Unikraft 既有的 PCI/virtio 基礎設施，處理 virtqueue descriptor ring 的讀寫、doorbell 通知、PCI MMIO，完全不修改直接重用。點擊後：292 KB 和 10 毫秒是因為這五層全部編譯成單一 binary，沒有 kernel、沒有 init、沒有模組系統。",
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

#pause

*DG5 — Evidence-Gated Claims*

- `gfx.kmscube.sw` PASS *≠* virgl / GPU acceleration
- `proto.real-driver` PASS *≠* QEMU execution
- Host Linux baseline *≠* Unikraft runtime

#metadata((
  t: "Note",
  v: "【初始】DG1（Standards-based）：嚴格遵循 OASIS VirtIO-GPU 規範，不發明私有協議。這確保 VOGUE 的 guest image 可以在任何支援標準 VirtIO-GPU 的 hypervisor 上執行（QEMU、crosvm、cloud-hypervisor），不需要特殊的 host 端修改。DG2（Small trusted substrate）：整個 guest 圖形棧約 5,000 LoC，比 Linux 三百萬行小了 600 倍，小到可以被完整審計。對安全敏感的 unikernel 部署很重要，你需要知道 TCB 裡每一行做什麼。DG3（Application source compatibility）：kmscube、llama.cpp 等 app 的原始碼完全不修改，compat shim 必須完整模擬 EGL、GLES2、GBM、DRM 的 API surface 和狀態機。DG4（Progressive validation）：由底往上分層驗證——native tests（不需要 QEMU）→ SW render → ABI 靜態檢查（Venus wire format）→ QEMU transport → accelerated render，每一層通過才往上，出問題可以精確定位在哪層。【點擊後】DG5（Evidence-Gated Claims）是方法論貢獻：嚴格區分不同層次的宣告。SW render PASS 不代表 GPU 加速能跑；ABI 靜態檢查 PASS 不代表 QEMU 執行沒問題；host Linux baseline 不能當 Unikraft 的效能證據。27 個 evidence row，18 個 PASS，9 個有明確記錄的阻塞原因和解鎖路徑。Blocked 不代表失敗，代表誠實記錄了已知的限制。",
)) <pdfpc>
