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
  [L1 Application], [kmscube / glmark2 / llama.cpp (*unmodified*)],
  [L2 Compat], [`libukegl` + `libukswrender` (2D display) · `libukggml_vk` (Vulkan dispatch)],
  [L3 Venus], [`libukvenus` (Vulkan → Venus binary)],
  [L4 VirtIO], [`libukvirtio_gpu` + `libukdma` (→ Unikraft virtqueue → QEMU)],
)

#pause

- Host: `QEMU virtio-gpu-gl-pci` → `virglrenderer` → Host Vulkan / GL driver
- Guest image size: *292 KB* (vs Linux kernel 9.2 MB — 31× smaller)
- Boot time: *10–11 ms* (vs Linux + initramfs 857–861 ms — 80× faster)

#metadata((
  t: "Note",
  v: "四層架構，每層精確定義如下。L1（Application）：上游原始碼不修改一行，直接編譯進 unikernel image；向下使用 EGL/GLES2（2D）、Vulkan（compute）或 virgl_encoder API（3D）。L2（Compat）：API 相容層，提供 app 在 Linux 上期待的 API surface，接進 VOGUE 實作；向上暴露 EGL/GLES2/Vulkan API，向下驅動 L3 或直接發 L4 命令；3D virgl 路徑不走這層（run_virgl_path 直接 include L4 的 virgl_encoder.h）；2D 用 libukegl + libukswrender，Vulkan compute 用 libukggml_vk。L3（Venus）：Vulkan 命令序列化層，把 Vulkan API 呼叫轉成 Venus PACKED binary stream；向上接受 libukggml_vk 發出的 Vulkan 呼叫，向下提交 Venus binary 給 L4 的 SUBMIT_3D；只存在於 Vulkan compute 路徑，2D 和 virgl 路徑跳過。L4（VirtIO）：VirtIO-GPU 協議的 guest 端實作，三條路徑的唯一交匯點；向上暴露 2D display 命令、SUBMIT_3D 和 virgl_encoder API，向下透過 Unikraft virtqueue 送命令給 QEMU；virtqueue 是其內部傳輸機制（LIBVIRTIO_BUS），不另立層。點擊後：292 KB 和 10 ms 是因為這四層全部編譯成單一 binary，無 kernel 模組、無 init、無動態載入。",
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
  ll(2, "Compat"),
  lbox("libEGL + libgbm + libdrm + Vulkan loader", "real Linux platform APIs"),
  [→],
  vbox("libukegl · libukswrender · libukggml_vk", "stub + CPU render + static Vk dispatch"),

  ll(3, "Venus"),
  lbox("Mesa Venus driver", "dynamic, Vulkan→Venus encoding"),
  [→],
  vbox("libukvenus", "same encoding, no Mesa"),

  ll(4, "VirtIO"),
  lbox("virtio-gpu + kernel virtio/PCI", "/dev/dri ioctl → kernel → virtqueue"),
  [→],
  vbox("libukvirtio_gpu", "direct virtqueue, no ioctl"),
)

#v(0.3em)

#align(center)[#block(fill: rgb("#d4edda"), stroke: 0.4pt, radius: 2pt, inset: (x: 10pt, y: 6pt), width: 82%)[
    *QEMU virtio-gpu* → *virglrenderer* → *Host GPU driver* #h(2em) _(shared)_]
]

#text(size: 0.75em)[≡ same · → replaced with leaner VOGUE equivalent]

#metadata((
  t: "Note",
  v: "這張圖把四層架構和 Linux 的對應一起展示。L1（App）完全不動。L2（Compat）：Linux 有真實的 libEGL/libgbm/libdrm 和 Vulkan loader（runtime dlopen ICD）；VOGUE 改用 libukegl/libukswrender（264 LoC display stub + CPU renderer）和靜態 dispatch table libukggml_vk，省掉動態載入。L3（Venus）：Linux 的 Mesa Venus driver 把 Vulkan 序列化成 Venus binary；VOGUE 的 libukvenus 邏輯等價，只是不依賴 Mesa。L4（VirtIO）：Linux 透過 /dev/dri ioctl 進 kernel 再到 virtqueue；VOGUE 直接操作 Unikraft virtqueue，省掉 ioctl overhead；virtqueue 本身是 libukvirtio_gpu 的內部機制，不另立層。Host 端（QEMU、virglrenderer、host GPU driver）完全不動，這就是為什麼 VOGUE 能跟現有 hypervisor 直接相容。",
)) <pdfpc>

== Three Execution Paths

#let pbox(fill, stk, title, sub) = block(
  fill: fill, stroke: stk, radius: 2pt,
  inset: (x: 6pt, y: 4pt), width: 100%,
)[*#title*#if sub != "" [\ #text(size: 0.65em, fill: luma(85), sub)]]

#let abox(title, sub) = pbox(rgb("#fff9c4"), 0.5pt, title, sub)
#let nbox(title, sub) = pbox(luma(245), 0.4pt, text(fill: luma(180), title), text(fill: luma(180), sub))
#let gbox(title, sub) = pbox(rgb("#d4edda"), 0.4pt, title, sub)

#let ll(n, name) = align(right, text(size: 0.62em, fill: luma(120))[L#n #name])
#let rl(name)    = align(right, text(size: 0.62em, fill: luma(120))[#name])

#grid(
  columns: (4.5em, 1fr, 1fr, 1fr),
  gutter: 8pt,
  align: (right + horizon, left, left, left),
  [],
  text(weight: "bold")[2D Display],
  text(weight: "bold")[3D Rendering],
  text(weight: "bold")[Vulkan Compute],

  ll(1, "App"),
  abox("app", "kmscube SW · glmark2"),
  abox("app", "kmscube virgl"),
  abox("app", "llama.cpp"),

  ll(2, "Compat"),
  abox("libukswrender", "CPU rasterizer"),
  nbox("—", "direct virgl_encoder call"),
  abox("libukggml_vk", "static Vk dispatch"),

  ll(3, "Venus"),
  nbox("—", "no Vulkan, CPU path only"),
  nbox("—", "virgl has no L3 serializer"),
  abox("libukvenus", "Vulkan → Venus binary"),

  ll(4, "VirtIO"),
  abox("TRANSFER_TO_HOST_2D", "SET_SCANOUT · FLUSH"),
  abox("SUBMIT_3D", "virgl_encoder.c in L4"),
  abox("SUBMIT_3D", "Venus payload"),

  rl("GPU"),
  nbox("none", "CPU only"),
  gbox("host GPU", "→ buffer"),
  gbox("host GPU", "→ buffer (no display)"),
)

#metadata((
  t: "Note",
  v: "這頁並排三條執行路徑，說明四層架構下每條路徑走哪些層、有沒有 GPU、輸出是什麼。2D display：CPU 用 libukswrender（L2）畫像素到記憶體，再透過 VirtIO-GPU 的 2D display 命令（L4）推送給 QEMU 顯示，整條路徑完全不涉及 GPU。3D rendering（virgl）：L2 和 L3 都是灰色的「—」，各有獨立的設計理由。為何不走 L2（Compat）：Gallium command stream 是 Mesa 的內部概念，不是公開 API——不像 Vulkan 有 vkCreateDevice 等乾淨的 function 邊界可攔截，app 直接產生 Gallium binary，Compat 層沒有立足點，所以 run_virgl_path 直接 include L4 的 virgl_encoder.h。為何不走 L3（Venus）：virgl 走的是 Gallium 協議，不是 Venus 協議，L3 的 libukvenus 只處理 Vulkan 序列化，完全不適用；此外 VOGUE 的 virgl encoding 故意最小化（virgl_encoder.c 只實作 create_surface、set_framebuffer、clear 共 3 個命令），這點程式碼放在 L4 就夠，建完整的 virgl L3 需要覆蓋整個 OpenGL 狀態機，違反 DG2，且 evidence 目標（gfx.kmscube.submit / gfx.kmscube.frame）只需要傳輸層證明，不需要完整 OpenGL 實作。因此這條路徑從 L1 直跳 L4，host GPU 渲染後輸出 buffer。Vulkan compute（llama.cpp）：libukggml_vk（L2）提供靜態 Vulkan dispatch table，libukvenus（L3）把 Vulkan 呼叫序列化成 Venus binary，透過 SUBMIT_3D 送給 virglrenderer，host GPU 執行矩陣運算，結果寫回 buffer，不產生畫面。",
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
