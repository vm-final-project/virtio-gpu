#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#let layer-stack(..active) = {
  let layers = (
    ("App",    "kmscube / llama.cpp"),
    ("Compat", "libukegl · swrender · ggml_vk"),
    ("Venus",  "libukvenus"),
    ("VirtIO", "libukvirtio_gpu → virtqueue"),
  )
  let active-set = active.pos()
  place(bottom + right, dy: -1.6em, dx: -1.6em,
    block(stroke: 0.4pt, inset: 5pt, radius: 3pt,
      stack(dir: ttb, spacing: 2pt,
        ..layers.map(l => {
          let highlighted = l.at(0) in active-set
          box(
            fill: if highlighted { rgb("#fff9c4") } else { luma(245) },
            inset: (x: 5pt, y: 3pt),
            radius: 2pt,
            text(size: 0.52em,
              if highlighted {
                strong(l.at(0) + ": " + l.at(1))
              } else {
                text(fill: luma(160), l.at(0))
              }
            )
          )
        })
      )
    )
  )
}

#outline-slide()

= Implementation I Driver Stack

== VirtIO-GPU: The Core Driver

#layer-stack("VirtIO")

*libukvirtio_gpu — full VirtIO-GPU protocol in ~1,650 LoC, exclusive device ownership*

*2D display pipeline* (enables kmscube, glmark2):

#align(center)[
  CPU draws pixels → push to QEMU → bind to display → wait
]

*Init once:* allocate host framebuffer, link to guest memory

#pause

*Insight: VirtIO-GPU executes commands in order*

→ waiting on "present" already implies "transfer" is done\
→ *one fence wait instead of two per frame*

#metadata((
  t: "Note",
  v: "【初始】libukvirtio_gpu 是整個系統的核心驅動，實作完整的 VirtIO-GPU 協議。因為 unikernel 只有一個客戶端，不需要 DRM 多工邏輯，整個驅動只需要 1,650 行。這頁展示最基本的 2D 顯示路徑：CPU 畫像素到記憶體，用 VirtIO-GPU 的 2D 命令推送給 host，顯示出來。這條路徑不涉及 GPU，目的是先確認 transport 管道正確，再往上加 GPU 複雜度。在這過程中發現一個優化：VirtIO-GPU 規範保證命令按順序完成，所以等 present 的 fence 就夠了，不需要再等 transfer 的 fence，每幀省掉一次同步等待。結果 kmscube 跑到 484 fps，glmark2 scene-clear 也通過了。",
)) <pdfpc>

== VirtIO-GPU Venus: GPU Acceleration

#layer-stack("Venus", "VirtIO")

*Venus extends VirtIO-GPU: Vulkan API calls serialized into binary → `SUBMIT_3D` → host GPU*

#pause

*Two mechanisms working together:*

#grid(columns: (1fr, 1fr), gutter: 1em,
  textbox[*Shared memory (L4)*
  - Guest and host map the *same* physical memory
  - Guest writes, host reads — *zero copy*
  - No DMA transfer needed],
  textbox[*Ring buffer (L3)*
  - Vulkan calls batched in shared memory
  - Host notified *once* per batch
  - Amortizes `SUBMIT_3D` cost],
)

*Result:* virgl pixel-correct frame ✓ · llama.cpp GPU at 160 tok/s ✓

_Protocol encoding verified with a software-only fake backend · end-to-end results on real QEMU + host GPU_

#metadata((
  t: "Note",
  v: "【初始】Venus 是 VirtIO-GPU 的 GPU 加速擴展，把 Vulkan API 呼叫序列化成 binary stream，透過 SUBMIT_3D 送到 host 端的 virglrenderer 執行。有兩個關鍵機制：shared memory 讓 guest 和 host 共享同一塊實體記憶體，guest 寫入 host 立刻看得到，零複製；ring buffer 讓 guest 把多條 Vulkan 命令打包，累積一批才通知 host 一次，避免每條命令都觸發一次昂貴的 virtqueue 操作。【點擊後】這頁有兩類結果要區分：virgl 像素正確幀和 llama.cpp 160 tok/s 是用真實的 QEMU + virglrenderer + host GPU 跑出來的（real backend）；fake backend 只用來驗證 Venus 協議編碼的正確性（ring buffer layout、命令格式），不涉及實際 GPU 執行。兩件事分開驗證，分開宣告。",
)) <pdfpc>
