#import "../lib/xwysyy-typst/xwysyy.typ": *
#import "../lib/xwysyy-typst/xwysyy-extras.typ": *

#let layer-stack(..active) = {
  let layers = (
    ("App",    "kmscube / llama.cpp"),
    ("Shim",   "libukegl / swrender"),
    ("Venus",  "libukvenus / ggml_vk"),
    ("VirtIO", "libukvirtio_gpu"),
    ("Trans",  "PCI / virtio"),
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

== GPU Command Transport: Vulkan Across the VM Boundary

#layer-stack("Venus", "VirtIO", "Trans")

*How do Vulkan API calls get from a unikernel to the host GPU?*

*Two mechanisms working together:*

#grid(columns: (1fr, 1fr), gutter: 1em,
  textbox[*Shared memory*
  - Guest and host map the *same* physical memory
  - Guest writes, host reads — *zero copy*
  - No DMA transfer needed],
  textbox[*Venus ring buffer*
  - Vulkan calls serialized into binary
  - Batched in shared memory
  - Host notified *once* per batch],
)

*Result:* virgl pixel-correct frame ✓ · llama.cpp GPU at 160 tok/s ✓

_Protocol correctness verified with a software-only fake backend — no QEMU needed_

#metadata((
  t: "Note",
  v: "這頁回答最核心的問題：Vulkan 命令怎麼從 unikernel 裡面到達 host 的 GPU？有兩個機制。第一個是 shared memory：guest 和 host 共享同一塊實體記憶體，guest 寫進去 host 立刻看得到，完全不需要複製。第二個是 Venus ring buffer：把多條 Vulkan 命令打包進這塊共享記憶體，累積一批之後才通知 host 一次。這兩個機制合起來讓 llama.cpp 跑到 160 tok/s，virgl 也能輸出像素正確的幀。最後一行補充：整個協議邏輯用純軟體的 fake backend 就能驗證，不需要 QEMU，讓開發和測試快很多。",
)) <pdfpc>

== Software Rendering — Proving the Plumbing

#layer-stack("Shim", "VirtIO")

*Before adding GPU complexity: verify the VirtIO-GPU transport end-to-end*

CPU draws pixels → push to QEMU → display _(no GPU involved)_

*Per frame:* init framebuffer once → draw → transfer → present → wait

*Result: kmscube at ~484 fps, glmark2 scene-clear ✓*

*Insight: VirtIO-GPU executes commands in order*

→ waiting on "present" already implies "transfer" is done\
→ *one fence wait instead of two per frame*

#metadata((
  t: "Note",
  v: "在加上 GPU 加速之前，我們先驗證 VirtIO-GPU 的傳輸管道是否正確。這條路徑是：CPU 畫像素到記憶體，用 VirtIO-GPU 的 2D 命令推送給 host，然後顯示出來，完全不涉及 GPU。目的是確認 transport 這層是通的，再往上疊加複雜度。結果 kmscube 跑到 484 fps，glmark2 也通過了。在這過程中發現一個優化：VirtIO-GPU 規範保證命令按順序完成，所以等 present 的 fence 就夠了，不需要再單獨等 transfer 的 fence。每幀省掉一次同步等待。",
)) <pdfpc>

== Implementing a Protocol Blind: The Venus Bug

#layer-stack("Venus")

*Implementing from a spec with no runtime feedback is hard*

Venus serializes Vulkan calls into a PACKED binary stream — no alignment padding\
A subtle rule: pointer presence = 8 bytes, *array* presence = *4 bytes*

*Bug: four array fields encoded at the wrong size*

#table(
  columns: (1.2fr, 1fr, 1fr),
  inset: 6pt,
  stroke: 0.5pt,
  table.header([Affected fields], [Wrong], [Correct]),
  [`ppEnabledExtensionNames`\ `ppEnabledLayerNames`\ `pQueueCreateInfos`\ `pQueuePriorities`],
  [pointer flag\ (8 bytes)],
  [array size\ (4 bytes)],
)

*Every field after them was shifted — but the host never complained*

virglrenderer *silently discards* malformed commands — no error, no crash\
Found by comparing against Mesa source · Fixed · *164 encoding checks ✓*

#metadata((
  t: "Note",
  v: "【初始】這頁說的是從規範實作協議有多難。Venus 是 PACKED 格式，沒有 alignment padding。有一個很容易混淆的細節：pointer 的存在性用 uint64（8 bytes）表示，但 array 的存在性用 uint32（4 bytes）表示。我們把四個 array 欄位搞錯了，每個多了 4 bytes，導致後面所有欄位的位移都錯誤。最難的地方是：virglrenderer 收到格式錯誤的命令時完全不報錯，靜靜丟掉。程式能跑，但什麼都渲染不出來。最後靠逐行比對 Mesa 的原始碼找到問題。修正後 164 項測試全部通過。",
)) <pdfpc>
