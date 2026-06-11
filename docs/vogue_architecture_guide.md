# VOGUE 專案完整架構指南：技術細節、原始碼對照與網路資源

## 一、整體架構概覽

VOGUE（**V**irtI**O**-GPU on Unikraft for **G**raphics and llama.cpp with V**U**lkan and V**E**nus）是一個研究型 artifact，核心目標是：

> 在 Unikraft unikernel 中，**不引入 Linux DRM/KMS 或 Mesa**，用自己寫的 4 個 thin library，透過 VirtIO-GPU + Venus 協定存取主機 GPU，執行 Vulkan 計算（llama.cpp 推論）與 3D 圖形（kmscube）。

### 完整技術堆疊（由上到下，每層對應本專案的程式碼）

```text
┌─────────────────────────────────────────────────────────────────────┐
│ 應用層  llama.cpp (bench/server) / kmscube                         │
│         apps/app-llama-upstream-vk/{bench,server}.cpp              │
│         apps/app-kmscube/main.c                                    │
│                    ↓ 使用標準 Vulkan API (vk*)                     │
├─────────────────────────────────────────────────────────────────────┤
│ libvulkan   — Vulkan ABI + Static Dispatch                         │
│         libs/libvulkan/uk_vulkan_dispatch.c                        │
│         libs/libvulkan/vk_hpp_loader.cpp                           │
│                    ↓ 路由到 Venus 驅動                             │
├─────────────────────────────────────────────────────────────────────┤
│ libukvulkan_venus  — Unikraft-native Venus Vulkan 驅動             │
│         libs/libukvulkan_venus/venus_driver.c   (入口)             │
│         libs/libukvulkan_venus/venus_cs.c       (command stream)   │
│         libs/libukvulkan_venus/venus_ring.c     (ring buffer)      │
│         libs/libukvulkan_venus/venus_compute.c  (compute hot path) │
│         libs/libukvulkan_venus/generated/       (codegen encoders) │
│                    ↓ Venus wire protocol 編碼 → SUBMIT_3D          │
├─────────────────────────────────────────────────────────────────────┤
│ libukvirtio_gpu  — VirtIO-GPU frontend + virgl encoder             │
│         libs/libukvirtio_gpu/virtio_gpu.c       (device driver)    │
│         libs/libukvirtio_gpu/virtio_gpu_proto.h (wire protocol)    │
│         libs/libukvirtio_gpu/virgl_encoder.c    (Gallium cmds)     │
│                    ↓ VIRTIO_GPU_CMD_* / virtqueue                  │
├═════════════════════════════════════════════════════════════════════┤
│              ━━━ 虛擬化邊界（Guest ↔ Host）━━━                     │
├═════════════════════════════════════════════════════════════════════┤
│ QEMU    virtio-gpu-gl-pci, blob=true, venus=true                   │
│         hw/display/virtio-gpu.c  (device model)                    │
│         hw/display/virtio-gpu-virgl.c  (Venus/virgl bridge)        │
│                    ↓ 傳遞到 virglrenderer                          │
├─────────────────────────────────────────────────────────────────────┤
│ virglrenderer   — Host-side Venus decoder                          │
│         src/venus/  (Venus command decode + host Vulkan dispatch)   │
│                    ↓ 呼叫主機 Vulkan 驅動                          │
├─────────────────────────────────────────────────────────────────────┤
│ 主機 GPU 驅動   e.g. NVIDIA V100（本專案評測環境）                 │
└─────────────────────────────────────────────────────────────────────┘
```

### "Dependency Collapse"（依賴崩縮）— 本專案的核心論點

Linux guest 使用 GPU 的完整路徑：
```
應用 → Mesa (user-space) → DRM ioctls → Linux DRM subsystem (13 objects)
     → GEM allocator → virtio-gpu kernel driver → virtqueue → QEMU
```

VOGUE 的路徑：
```
應用 → libvulkan (1 file) → libukvulkan_venus (12 files) → libukvirtio_gpu (7 files) → QEMU
```

兩者說一樣的 VirtIO-GPU 協定，但 VOGUE 只需 **4 個 library**，Linux 需要 **13+ 個 DRM/KMS/GEM kernel objects + 整個 Mesa**。

> 📖 這在 `docs/ARCHITECTURE.md` 的 View A（"The Collapse"）有視覺化呈現。

---

## 二、各技術層：程式碼細節 + 技術概念 + 參考資源

---

### 【層 1】Unikraft — Library OS / Unikernel 基礎

#### 在專案中的角色

Unikraft 是整個 VOGUE 的「作業系統」。它不用 Linux kernel，而是把 OS 功能拆成可插拔的 micro-library，只編入應用需要的部分，產出一個單一的 unikernel image。

#### 關鍵概念你需要懂

| 概念 | 說明 |
|------|------|
| **Unikernel 模型** | 應用 + OS 在同一個 address space，沒有 user/kernel mode 切換，沒有 context switch 開銷 |
| **Micro-library 架構** | 每個 OS 功能（scheduler、allocator、network stack、filesystem）都是一個獨立的 library，可以透過 Kconfig 選擇要不要編入 |
| **Kconfig 設定** | 和 Linux kernel 一樣的設定系統，每個 library 的 `Config.uk` 定義可選項 |
| **KraftKit** | CLI 工具（類似 cargo/npm），用 `Kraftfile` 描述 appliance 的 library 組合、平台、架構 |
| **單一用途映像** | "One image, one purpose" — 每個 appliance 只啟動一個 entrypoint，不含 shell 或 fork/exec |

#### 本專案涉及的 Unikraft 元件

| Unikraft Library | 用途 | 在 plan-optimize.md 的角色 |
|---|---|---|
| `ukschedcoop` | 協作式排程器（目前唯一可用） | P0 前提：沒有搶佔式排程 |
| `uklcpu` / `ukpcpuvar` | SMP / per-CPU 變數支援 | P0 目標：多 vCPU 支援 |
| `ukalloc` / `ukallocbbuddy` | 記憶體分配器（buddy allocator） | P3：想換成 mimalloc 但被 musl/newlib 衝突擋住 |
| `uksglist` | Scatter-gather list（DMA 用） | 取代自製 DMA library，用於 GPU buffer 記憶體 |
| `ukfs-virtiofs` | VirtioFS guest-side 支援 | P1：VirtioFS 替代 9pfs 的 candidate |
| `lib-lwip` | lwIP TCP/IP stack | llama server HTTP 網路 |
| `lib-musl` | musl libc | C library 提供 |

#### 本專案的 Kraftfile

```
kraft/Kraftfile.llama-upstream-vk          → Vulkan bench appliance
kraft/Kraftfile.llama-upstream-vk-server   → Vulkan HTTP server appliance
kraft/Kraftfile.llama-upstream-bench       → CPU bench
kraft/Kraftfile.llama-upstream-server      → CPU server
Kraftfile（根目錄）                         → kmscube 圖形 appliance
```

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **Unikraft 架構文件（必讀）** | https://unikraft.org/docs/internals/architecture | micro-library 模型、boot 流程、平台抽象層、Kconfig 系統 |
| **Unikraft 效能文件** | https://unikraft.org/docs/concepts/performance | 為什麼 unikernel 比 Linux VM 快、allocator 比較、boot time |
| **Unikraft GitHub** | https://github.com/unikraft/unikraft | 核心原始碼。本專案 pin 在 `../unikraft` branch `stable` commit `7351f8b` |
| **KraftKit CLI 文件** | https://unikraft.org/docs/cli/reference/kraftkit | `kraft build`、`kraft run` 指令說明 |
| **KraftKit GitHub** | https://github.com/unikraft/kraftkit | KraftKit 原始碼和 issue tracker |
| **USENIX ATC'21 論文（必讀）** | https://www.usenix.org/conference/atc21/presentation/kuenzer | 學術論文 "Unikraft: Fast, Specialized Unikernels the Easy Way"。理解設計理念的最佳起點 |
| **Unikraft Catalog** | https://github.com/unikraft/catalog | 其他應用的 Unikraft port 範例（nginx, redis, SQLite 等），了解 porting 的標準流程 |
| **Unikraft Mimalloc 評測** | https://unikraft.org/blog/2024-08-22-unikraft-gsoc-benchmarking-mimalloc | plan-optimize.md P3 引用。allocator 效能分析 |
| **Unikraft 官方文件首頁** | https://unikraft.org/docs | 完整的文件目錄 |

---

### 【層 2】VirtIO-GPU — 半虛擬化 GPU I/O

#### 在專案中的角色

`libukvirtio_gpu` 實作 VirtIO-GPU 前端驅動，等同 Linux kernel 中的 `drivers/gpu/drm/virtio/`，但完全不依賴 Linux DRM。它透過 virtqueue 向 QEMU 發送 GPU 命令。

#### 關鍵原始碼

| 檔案 | 行數 | 功能 |
|------|------|------|
| **`libs/libukvirtio_gpu/virtio_gpu.c`** | ~770 | **主驅動**：初始化 PCI 設備、協商 feature bits（`VIRTIO_GPU_F_VIRGL`, `VIRTIO_GPU_F_RESOURCE_BLOB`, `VIRTIO_GPU_F_CONTEXT_INIT`）、建立 virtqueue（controlq/cursorq）、實作資源生命週期 |
| **`libs/libukvirtio_gpu/virtio_gpu_proto.h`** | ~344 | **Wire 協定定義**：所有 `VIRTIO_GPU_CMD_*` 命令碼、`VIRTIO_GPU_F_*` feature bits、command header struct。**直接對應 VirtIO spec Section 5.7** |
| **`libs/libukvirtio_gpu/virtio_gpu_internal.h`** | ~150 | 內部設備狀態：`struct virtio_gpu_dev`、fence 追蹤、capset 管理、blob allocation |
| **`libs/libukvirtio_gpu/virgl_encoder.c`** | ~660 | **Virgl 3D 命令編碼器**：產生 Gallium 層級的 3D 指令（kmscube 路徑用） |
| **`libs/libukvirtio_gpu/virgl_hw.h`** | ~220 | Virgl 硬體定義：`VIRGL_CCMD_*` 指令碼、capset 結構（V1/V2/Venus）、資源類型 |
| **`libs/libukvirtio_gpu/virtio_gpu_pci.c`** | ~160 | PCI probing：匹配 `PCI_VENDOR_VIRTIO` + `PCI_DEVICE_VIRTIO_GPU` |
| **`libs/libukvirtio_gpu/virtio_gpu_capsets.c`** | ~100 | Capset 協商：`virtio_gpu_get_capset_info`、`virtio_gpu_get_capset` — 探測 virgl/venus 支援 |
| **`libs/libukvirtio_gpu/fake_virtio_gpu_backend.c`** | ~400 | **測試用 fake backend**：記憶體中模擬 VirtIO-GPU 設備，追蹤已提交的命令、資源、fence。host-native 測試不需 QEMU/GPU |

> **注意（最新狀態）**：上表反映較早的分檔佈局。目前 `libs/libukvirtio_gpu/` 的實際來源檔已整併為 `virtio_gpu_real.c`、`virtio_gpu_real_priv.h`、`virgl_encoder.c`、`virtio_gpu_proto.h`（其餘 `virtio_gpu.c` / `*_pci.c` / `*_capsets.c` / `fake_*` 等檔名已不存在）。`virtio_pci_shm_region_get()` 不在 `libukvirtio_gpu` 內，而是由 modern virtio-pci 傳輸層提供（`patches/unikraft/0001-virtio-pci-modern-device-support.patch`，解析 cfg_type 8 共享記憶體 capability），並由 `virtio_gpu_real.c` 以 `extern` 呼叫；該 host-visible blob 路徑在軟體 host Vulkan 驅動上無法完成，故 llama Vulkan appliance 以 `--no-host` 讓權重保持 device-local（見 README「x86_64 Vulkan-server host bring-up」與 `docs/VENUS-BRINGUP.md`）。

#### 關鍵函式簽名

```c
// virtio_gpu.c — 設備初始化
int virtio_gpu_init(struct virtio_gpu_dev *dev);

// virtio_gpu.c — 送出 3D 指令（Venus/virgl 的主要通道）
int virtio_gpu_submit_3d(struct virtio_gpu_dev *dev,
                         uint32_t ctx_id, void *buf, uint32_t size);

// virtio_gpu.c — 建立 blob 資源（host-visible 共享記憶體）
int virtio_gpu_resource_create_blob(struct virtio_gpu_dev *dev, ...);

// virtio_gpu.c — 同步等待 fence
int virtio_gpu_wait_fence(struct virtio_gpu_dev *dev, uint64_t fence_id);
```

#### 關鍵概念你需要懂

| 概念 | 說明 | 對應程式碼 |
|------|------|-----------|
| **VirtIO** | 半虛擬化（paravirtualization）I/O 框架。Guest 知道自己在 VM 裡，與 hypervisor 合作而非模擬真硬體 | `virtio_gpu.c` 的 virtqueue 初始化 |
| **Virtqueue** | VirtIO 的 I/O 通道。用 descriptor table + available ring + used ring 在 guest/host 間傳遞資料 | `virtio_gpu.c` 建立 controlq/cursorq |
| **VIRTIO_GPU_CMD_SUBMIT_3D** | 最重要的命令：把 3D 指令（virgl 或 Venus 編碼的）送到 host | `virtio_gpu_submit_3d()` |
| **Feature bits** | 設備能力協商。`F_VIRGL`=3D 支援、`F_RESOURCE_BLOB`=blob 資源、`F_CONTEXT_INIT`=多 context 支援 | `virtio_gpu_proto.h` 定義、`virtio_gpu.c` 中協商 |
| **Blob resources** | 一塊 guest/host 共享的記憶體區域，用於高效資料傳輸。Venus 用 blob 做 host-visible buffer | `virtio_gpu_resource_create_blob()` |
| **Capsets (capability sets)** | 設備功能集：virgl capset V1/V2 用於 OpenGL，Venus capset 用於 Vulkan | `virtio_gpu_capsets.c`, `virgl_hw.h` |
| **Fences** | 同步機制：guest 送出指令後，透過 fence 知道 host 何時執行完畢 | `virtio_gpu_wait_fence()` |

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **OASIS VirtIO v1.2 規格（必讀 §5.7）** | https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html | VirtIO-GPU 設備的完整規格：命令格式、feature bits、resource 管理、blob 支援。`virtio_gpu_proto.h` 直接鏡像此規格 |
| **VirtIO v1.3 Draft** | https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html | 最新草案，包含 Venus capset 和 blob 資源的擴展定義 |
| **QEMU VirtIO-GPU 文件** | https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html | QEMU 端的 `virtio-gpu-gl-pci` 選項說明：`venus=true`, `blob=true`, `egl-headless` |
| **Gerd Hoffmann 的部落格** | https://www.kraxel.org/blog/tag/virtio/ | QEMU virtio-gpu 維護者的技術文章，涵蓋設備演進、3D 支援、blob 資源 |
| **Linux VirtIO-GPU 驅動原始碼** | https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/virtio | Linux 的參考實作。`virtgpu_vq.c` 對標 VOGUE 的 `virtio_gpu.c`、`virtgpu_submit.c` 對標 `virtio_gpu_submit_3d` |
| **OSDev Wiki: VirtIO** | https://wiki.osdev.org/Virtio | 清楚解釋 virtqueue 機制（descriptor table、available/used ring），有助理解 `virtio_gpu.c` 的 virtqueue 設定 |
| **VirtIO 規格 §2（通用）** | 同上 OASIS 連結 §2 | VirtIO 通用機制：PCI 設備發現、feature 協商、virtqueue 建立、中斷處理 |
| **QEMU 原始碼: hw/display/virtio-gpu.c** | https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu.c | Host-side 設備模型：命令分發、resource 管理、blob 支援 |
| **QEMU 原始碼: hw/display/virtio-gpu-virgl.c** | https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu-virgl.c | QEMU 的 Venus/virgl 橋接：處理 SUBMIT_3D 轉發到 virglrenderer |
| **Blog: Setting up Venus on QEMU** | https://tm23forest.com/contents/virtio-venus-qemu-virglrenderer-vulkan-passthrough | 逐步指南，啟用 QEMU + virglrenderer + Venus。與 VOGUE 的 QEMU 命令列吻合 |

---

### 【層 3】Venus 協定 — Vulkan 虛擬化的核心技術

#### 在專案中的角色

`libukvulkan_venus` 是 VOGUE **自己從頭實作**的 Venus Vulkan 驅動，取代 Linux guest 中 Mesa 的 `venus` ICD。它把 Vulkan API 呼叫序列化成 Venus wire format，透過 `libukvirtio_gpu` 的 SUBMIT_3D 送到 host。

#### 關鍵原始碼

| 檔案 | 行數 | 功能 |
|------|------|------|
| **`venus_driver.c`** | ~530 | **驅動入口**：`uk_vulkan_venus_open()` 初始化 VirtIO-GPU、建立 Venus context、探測 capset、設定 ring/batch 模式。`uk_vulkan_venus_close()` 清理 |
| **`venus_cs.c`** | ~450 | **Command stream**（命令流）：`cmd_submit_locked()` 是熱路徑核心 — 序列化 Venus 命令並呼叫 `virtio_gpu_submit_3d()`。處理 ring vs batch 模式、fence 追蹤（`completed_fence`）、WaitFences poll 優化（#2） |
| **`venus_ring.c`** | ~280 | **Ring buffer 實作**：`uk_venus_ring_create()`（lazy，在第一個 cmd buffer 時建立）、`uk_venus_ring_cmd_write()`（串流寫入）、`uk_venus_ring_cmd_wait()`（spin-wait with `pause`） |
| **`venus_init.c`** | ~380 | Venus 初始化序列：`vkCreateInstance`、`vkCreateDevice`、`vkEnumeratePhysicalDevices` 的 wire 編碼 |
| **`venus_compute.c`** | ~320 | **Compute 熱路徑**：`vkCreateComputePipelines`、`vkCmdDispatch`、`vkCmdBindPipeline`、`vkCmdBindDescriptorSets`。ggml-vulkan 的主要通道 |
| **`venus_memory.c`** | ~250 | 記憶體管理：`vkAllocateMemory`、`vkMapMemory`、`vkBindBufferMemory`。映射 host-visible blob 到 Vulkan buffer |
| **`venus_descriptor.c`** | ~200 | Descriptor set：`vkCreateDescriptorSetLayout`、`vkAllocateDescriptorSets`、`vkUpdateDescriptorSets` |
| **`venus_buffer.c`** | ~180 | Buffer 操作：`vkCreateBuffer`、`vkDestroyBuffer`、buffer memory requirements |
| **`venus_sync.c`** | ~170 | 同步原語：`vkCreateFence`、`vkWaitForFences`（with `completed_fence` poll）、`vkCreateSemaphore`、`vkResetFences` |
| **`venus_cmd.c`** | ~250 | Command buffer：`vkAllocateCommandBuffers`、`vkBeginCommandBuffer`、`vkEndCommandBuffer`（ring flush）、`vkCmdCopyBuffer`、`vkCmdPipelineBarrier` |
| **`venus_queue.c`** | ~120 | Queue：`vkQueueSubmit`（ring 模式下做 single final flush）、`vkGetDeviceQueue` |
| **`venus_pipeline.c`** | ~150 | Pipeline state：`vkCreatePipelineLayout`、`vkCreateShaderModule`（SPIR-V） |
| **`venus_query.c`** | ~100 | Query pool 和 timestamp 命令 |
| **`venus_wire.h`** | ~180 | Wire 格式：Venus command header struct、opcode ID、reply 格式定義 |
| **`venus_types.h`** | ~120 | 內部型別：`struct uk_venus_device`、`struct uk_venus_ring`、模式 flag |
| **`generated/`** | 目錄 | **自動產生的 Venus 編碼器**：從 `../venus-protocol` XML 生成。每個 Vulkan 呼叫對應一個 `venus_encode_*.h`。手動從上游生成器重新產生（見 `GENERATOR.md`） |
| **`GENERATOR.md`** | — | 描述從 venus-protocol XML 到 C header 的 codegen 流程 |

#### 關鍵函式簽名與呼叫流程

```c
// venus_driver.c — 初始化整個 Venus 堆疊
int uk_vulkan_venus_open(struct uk_venus_device **dev);

// venus_cs.c — 熱路徑命令提交（每個 Vulkan 呼叫最終走這裡）
int cmd_submit_locked(struct uk_venus_device *dev,
                      void *cmd, size_t len,
                      uint64_t *fence_out);

// venus_ring.c — Ring 串流寫入
int uk_venus_ring_cmd_write(struct uk_venus_ring *ring,
                            void *data, size_t len);

// venus_ring.c — Ring 建立（lazy，在第一次 command buffer 時觸發）
int uk_venus_ring_create(struct uk_venus_device *dev);
```

**一次 Vulkan compute dispatch 的完整呼叫鏈：**
```
應用呼叫 vkCmdDispatch()
  → libvulkan/uk_vulkan_dispatch.c      將呼叫路由到 Venus
  → libukvulkan_venus/venus_compute.c   序列化成 Venus wire format
  → libukvulkan_venus/venus_cs.c        cmd_submit_locked()
    → 若 ring 模式: venus_ring.c         uk_venus_ring_cmd_write()
    → 若 batch 模式: venus_cs.c          virtio_gpu_submit_3d() → SUBMIT_3D
  → libukvirtio_gpu/virtio_gpu.c        通過 virtqueue 送到 QEMU
  → QEMU → virglrenderer → host Vulkan
```

#### Venus 傳輸優化（README 中的熱路徑優化，plan-optimize.md 背景）

| # | 優化 | 機制 | 效果 | 對應程式碼 |
|---|------|------|------|-----------|
| 1 | 合併 EndCmdBuf + QueueSubmit + WaitFences | ring 模式下 `vkEndCommandBuffer` flush tail → `vkQueueSubmit` 做 single final flush | 3 kicks → 1 kick/step | `venus_cmd.c`, `venus_queue.c` |
| 2 | `vkWaitForFences` 用 `completed_fence` poll | `cmd_submit_locked()` 已同步標記 `completed_fence`，WaitFences 讀本地值 | 省掉 1 次 SUBMIT_3D/step | `venus_sync.c`, `venus_cs.c` |
| 3 | `__asm__("pause")` 在 busy-poll | spin-wait 時讓出 core 給 host `ring_thread` | 減少 single vCPU 上的 spin 壓力 | `venus_ring.c`, `venus_cs.c` |
| 4 | Ring stream 模型 | `vkCmd*` 直接寫入 host-visible ring circular buffer，host 非同步 drain | 0 malloc、request-response → streaming | `venus_ring.c` |

Native 測試（`make -C tests venus-ring-core`）的量化結果：

| 模式 | SUBMIT_3D / step | vs per-call |
|------|-------------------|-------------|
| A — per-call（pre-opt） | **10.00** | 1× |
| B — batch + WaitFences poll (#1 partial, #2) | **2.00** | **5.0× fewer** |
| C — ring stream (#1 + #4) | **1.02** | **9.85× fewer** |

#### 關鍵概念你需要懂

| 概念 | 說明 |
|------|------|
| **Venus 協定** | Vulkan 指令的二進位序列化格式。把 `vkCmdDispatch` 等 API 呼叫編碼成固定格式的 byte stream，透過 SUBMIT_3D 送到 host |
| **Wire format** | 每個 Venus 命令有 header（opcode + size）+ payload（Vulkan 結構的序列化）。`venus_wire.h` 定義 |
| **Ring buffer vs Batch** | **Ring**：guest 寫入 host-visible 環形緩衝區，host `ring_thread` 非同步讀取。**Batch**：每批命令呼叫一次 SUBMIT_3D。Ring 理論上更快但在 QEMU 11 + virglrenderer 上受限於 host-side latency |
| **Capset negotiation** | Guest 透過 `GET_CAPSET_INFO` / `GET_CAPSET` 命令探測 host 是否支援 Venus |
| **Venus context** | 一個 Venus 工作階段。Guest 建立 context 後，所有 Vulkan 命令都在此 context 內發送 |
| **Code generation** | `../venus-protocol` 的 XML 定義每個 Vulkan 命令的 wire 編碼。VOGUE 用 script 生成 `generated/venus_encode_*.h` |

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **Collabora Blog: Venus 技術文章（必讀）** | https://www.collabora.com/news-and-blog/blog/2021/10/12/venus-a-new-vulkan-driver-for-virtualized-gpus/ | Venus 架構深入解說：thin-layer 設計、wire format、ring buffer 模型、host-guest 同步。最完整的技術介紹 |
| **Collabora Blog: Venus on VirtIO-GPU NEXT** | https://www.collabora.com/news-and-blog/blog/2023/04/28/venus-on-virtio-gpu-next/ | Venus 進階：blob memory、cross-device memory、效能優化 |
| **Mesa Venus 驅動文件** | https://docs.mesa3d.org/drivers/venus.html | Mesa 官方 Venus ICD 說明。VOGUE 重新實作了相同的功能 |
| **venus-protocol GitLab** | https://gitlab.freedesktop.org/vn/venus-protocol | 協定定義 XML。本專案的 codegen 來源（手動生成，見 `GENERATOR.md`） |
| **Mesa 原始碼: vn_ring.c** | https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/virtio/vulkan/vn_ring.c | Mesa 的 ring buffer 實作。VOGUE 的 `venus_ring.c` 鏡像此設計 |
| **Mesa 原始碼: vn_renderer_virtgpu.c** | https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/virtio/vulkan/vn_renderer_virtgpu.c | Mesa 怎麼透過 Linux DRM 到 Venus。VOGUE 跳過此層，用 native `libukvirtio_gpu` |
| **FOSDEM 2022: Venus – Vulkan in VMs** | https://archive.fosdem.org/2022/schedule/event/vai_venus/ | 會議演講（有投影片和影片），解釋 Venus 架構和效能目標 |
| **Collabora: Scalable Virtual GPU** | https://www.collabora.com/news-and-blog/blog/2023/06/08/scalable-virtual-gpu-containers-vms/ | virglrenderer 的多 context 支援和容器/VM 下的 GPU 虛擬化 |
| **virglrenderer GitLab** | https://gitlab.freedesktop.org/virgl/virglrenderer | Host-side 渲染後端原始碼（包含 Venus decoder） |
| **virglrenderer Venus 原始碼** | https://gitlab.freedesktop.org/virgl/virglrenderer/-/tree/master/src/venus | Host-side Venus 命令解碼器 |

---

### 【層 4】Vulkan API Dispatch — libvulkan

#### 在專案中的角色

`libvulkan` 是應用程式看到的 Vulkan 介面。它實作所有 `vk*` 函式符號，並靜態路由到 `libukvulkan_venus`。等同 Khronos Vulkan Loader，但因為 unikernel 是靜態連結，不需要動態載入 ICD。

#### 關鍵原始碼

| 檔案 | 行數 | 功能 |
|------|------|------|
| **`libs/libvulkan/uk_vulkan_dispatch.c`** | ~400 | **靜態 dispatch table**：export 所有 `vk*` 符號（`vkCreateInstance`、`vkCmdDispatch`…），每個函式直接呼叫 `libukvulkan_venus` 的對應實作。這是 unikernel 版的 "Vulkan loader" |
| **`libs/libvulkan/vk_hpp_loader.cpp`** | ~200 | **Vulkan-Hpp C++ dispatcher**：提供 `vk::DispatchLoaderDynamic` 介面，讓 upstream `ggml-vulkan.cpp` 可以用 C++ Vulkan API。從 static dispatch table 解析函式指標 |
| **`libs/libvulkan/vulkan_unikraft.h`** | ~80 | Unikraft 專用 Vulkan 平台 header。定義 `VK_USE_PLATFORM_UNIKRAFT_KHR` |

#### 關鍵概念你需要懂

| 概念 | 說明 |
|------|------|
| **Vulkan Loader / ICD 分層** | Vulkan 標準架構：Application → Loader → Layers (validation/debug) → ICD (Installable Client Driver)。VOGUE 簡化為：Application → static dispatch table → Venus driver |
| **Static vs Dynamic dispatch** | 一般 Linux 用 `libvulkan.so` 動態載入 ICD。VOGUE 直接在編譯時靜態連結，不需 `libvulkan.so` |
| **Compute-first subset** | VOGUE 只實作 Vulkan compute pipeline 需要的呼叫（CreateComputePipelines, CmdDispatch 等），不實作 graphics pipeline（RenderPass, DrawIndexed 等） |
| **Vulkan-Hpp** | C++ Vulkan wrapper。`ggml-vulkan.cpp` 用 `vk::DispatchLoaderDynamic` 取得函式指標 |

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **Vulkan 規格（完整 API reference）** | https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html | 所有 Vulkan API 的完整定義。查看 VOGUE 實作了哪些 |
| **Vulkan Loader 架構（必讀）** | https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md | 解釋 loader → layer → ICD 的 dispatch 機制。VOGUE 的 `libvulkan` 是此架構的靜態版 |
| **Vulkan-Hpp GitHub** | https://github.com/KhronosGroup/Vulkan-Hpp | C++ binding，`vk_hpp_loader.cpp` 使用 |
| **Vulkan Tutorial（入門）** | https://vulkan-tutorial.com/ | 若不熟悉 Vulkan：instance、device、queue、command buffer 的基本概念 |
| **Khronos Vulkan Guide** | https://github.com/KhronosGroup/Vulkan-Guide | 官方學習資源，包含 compute pipeline、memory management |

---

### 【層 5】應用層 — llama.cpp / kmscube

#### 在專案中的角色

完全使用**未修改的上游原始碼**。透過 6 個 one-line `#include` shim 和 cross-compile toolchain（`cmake/unikraft-clang.cmake`）整合到 Unikraft image 中。

#### 關鍵原始碼

| 檔案 | 功能 |
|------|------|
| **`apps/app-llama-upstream-vk/bench.cpp`** | Vulkan bench entrypoint：直接呼叫 upstream `llama_bench()` |
| **`apps/app-llama-upstream-vk/server.cpp`** | Vulkan HTTP server entrypoint：呼叫 upstream `llama_server()`，設定 `--parallel 4`、`--flash-attn off`、監聽 `0.0.0.0:8080` |
| **`apps/app-llama-upstream-vk/common.h`** | 共用設定：`use_mmap=false`（因為 9pfs 不支援 mmap）、`huge_pages=0` |
| **`apps/app-llama-upstream-vk/Makefile.uk`** | **Unikraft build 整合**：編譯 upstream `ggml-vulkan.cpp` + SPIR-V shader blobs、`-march=$(LLAMA_MARCH) -mtune=$(LLAMA_MARCH)` |
| **`apps/app-llama-upstream/bench.cpp`** | CPU bench entrypoint |
| **`apps/app-llama-upstream/server.cpp`** | CPU server entrypoint |
| **`apps/app-kmscube/main.c`** | VirtIO-GPU 3D 圖形 demo：透過 virgl/Gallium 路徑渲染旋轉方塊 |
| **`apps/app-vulkan-smoke/main.c`** | 最小 Vulkan 冒煙測試：create instance + device + compute pipeline |

#### 網路/檔案系統技術

| 技術 | 在專案中的使用 | 對應設定 |
|------|---------------|---------|
| **lwIP TCP/IP stack** | llama server 的 HTTP 網路。virtio-net → libuknetdev → lwIP → DHCP → TCP listen on 8080 | `kraft/Kraftfile.llama-upstream-vk-server` 的 Kconfig |
| **9pfs (VirtIO-9P)** | Guest 掛載 host 資料夾（model 檔）。`max_msize ≈ 520 KB`，已與 virtqueue descriptor 數匹配（不可安全調大） | `../unikraft/drivers/virtio/9p/virtio_9p.c:46,106,308` |
| **VirtioFS** | **P1 候選**：替代 9pfs 以支援 mmap。Guest-side `../unikraft/lib/ukfs-virtiofs/` 存在但 host `virtiofsd` 未安裝 | 目前 blocked |

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **llama.cpp GitHub** | https://github.com/ggml-org/llama.cpp | upstream 原始碼。本專案從 `../llama.cpp` include |
| **llama.cpp Server README** | https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md | server 啟動參數（`--parallel`, `--threads`, `--flash-attn`），plan-optimize.md 引用 |
| **llama.cpp Build Docs** | https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md | Build system，包含 Vulkan backend flags |
| **llama.cpp Speculative Decoding** | https://github.com/ggml-org/llama.cpp/blob/master/docs/speculative.md | plan-optimize.md P4 引用 |
| **ggml-vulkan.cpp 原始碼** | https://github.com/ggml-org/llama.cpp/blob/master/ggml/src/ggml-vulkan/ggml-vulkan.cpp | Vulkan compute backend。VOGUE 在 tree 內編譯此檔，`vk*` 呼叫路由到 `libvulkan` |
| **ggml CMakeLists.txt** | https://github.com/ggml-org/llama.cpp/blob/master/ggml/CMakeLists.txt | plan-optimize.md P2 引用：`GGML_NATIVE` cross-compile 預設值 |
| **virtio-fs 官方** | https://virtio-fs.gitlab.io/ | VirtioFS 架構、DAX window、FUSE passthrough。plan-optimize.md P1 引用 |
| **virtiofsd（Rust 實作）** | https://gitlab.com/virtio-fs/virtiofsd | VirtioFS 需要的 host daemon。VOGUE 評測環境上未安裝（P1 blocker） |
| **QEMU 9pfs 文件** | https://wiki.qemu.org/Documentation/9p | 9pfs 共享檔案系統說明 |
| **VirtioFS 設計 (KVM Forum)** | https://static.sched.com/hosted_files/kvmforum2019/0e/virtio-fs_%20A%20Shared%20File%20System%20for%20Virtual%20Machines.pdf | VirtioFS 設計簡報：DAX、FUSE、virtiofsd 架構 |
| **lwIP 官方** | https://savannah.nongnu.org/projects/lwip/ | 輕量 TCP/IP stack |
| **lwIP on Unikraft** | https://github.com/unikraft/lib-lwip | Unikraft 版 lwIP |
| **lwIP API 文件** | https://www.nongnu.org/lwip/2_1_x/index.html | raw/netconn/socket 介面 |

---

### 【層 6】DRM/KMS — VOGUE 刻意跳過的 Linux 子系統

#### 為什麼需要了解

雖然 VOGUE 不使用 Linux DRM/KMS，但理解它有助理解 VOGUE 省掉了什麼。Linux guest 要用 GPU 需經過 DRM (Direct Rendering Manager) + KMS (Kernel Mode Setting) + GEM (Graphics Execution Manager)，這涉及 13+ 個 kernel objects 和整個 Mesa userspace。

#### VOGUE 的替代路徑

| Linux 需要 | VOGUE 的對應 |
|-----------|-------------|
| `/dev/dri/renderD128` (render node) | 不需要。`uk_vulkan_venus_open()` 直接呼叫 `libukvirtio_gpu` |
| `drm_ioctl(DRM_IOCTL_VIRTGPU_EXECBUFFER)` | `virtio_gpu_submit_3d()` 直接操作 virtqueue |
| GEM buffer 管理 | `virtio_gpu_resource_create_blob()` |
| Mesa ICD (`vulkan_virtio.so`) | `libukvulkan_venus` 靜態連結 |
| DRM virtgpu kernel driver | `libukvirtio_gpu`（但沒有 DRM 抽象） |

本專案有一個**可選的** DRM 相容 shim `libs/libukvirtgpu_drm/`，預設關閉（`CONFIG_LIBUKVULKAN_VENUS_USE_DRM_COMPAT = n`）。

#### 🌐 參考資源

| 資源 | URL | 需要理解的內容 |
|------|-----|---------------|
| **Linux DRM Internals** | https://www.kernel.org/doc/html/latest/gpu/drm-internals.html | DRM 子系統架構 |
| **DRM/KMS Overview** | https://www.kernel.org/doc/html/latest/gpu/introduction.html | Direct Rendering Manager + Kernel Mode Setting 介紹 |
| **Linux virtio-gpu DRM 驅動** | https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/virtio/virtgpu_vq.c | Linux 實作。與 `libukvirtio_gpu/virtio_gpu.c` 做功能對照 |
| **Linux Graphics Stack 2023** | https://blog.mecheye.net/2023/09/linux-graphics-stack-2023/ | 全面解說 Linux 圖形堆疊 — 理解 VOGUE 移除了多少層 |

---

## 三、測試架構

| 測試檔案 | 對應層 | 測試內容 |
|---------|--------|---------|
| **`tests/virtio_gpu_proto_test.c`** | 層 2 (VirtIO-GPU) | Wire ABI：struct 大小、feature bits、command layout 是否符合 VirtIO spec |
| **`tests/virtio_gpu_core_test.c`** | 層 2 | 核心操作：init、resource create/destroy、submit 3D、fences（用 fake backend） |
| **`tests/venus_cs_test.c`** | 層 3 (Venus) | Command stream：submit、fence tracking、batch/ring modes |
| **`tests/venus_hotpath_test.c`** | 層 3 | **傳輸優化量測**：計算每個推論步驟的 SUBMIT_3D 次數（A/B/C 三種模式） |
| **`tests/virgl_enc_test.c`** | 層 2 (Virgl) | Virgl encoder 正確性：command 編碼、draw state |
| **`tests/test_dispatch.c`** | 層 4 (Dispatch) | ggml-vulkan dispatch：驗證 `vk*` 符號透過 dispatch table 可以正確解析 |
| **`tests/vulkan_api_coverage_test.c`** | 層 4 | API coverage：斷言 `ggml-vulkan.cpp` 用到的所有 Vulkan 呼叫都存在 |

**執行方式：**
```bash
make test-fast     # 日常 gate（governance + docs + native tests，不需 GPU）
make native-tests  # 完整 host-native C 套件
make test-core     # 只跑 core / venus / dispatch 群組
```

---

## 四、專案內部重要文件索引

| 文件 | 路徑 | 內容 |
|------|------|------|
| **ARCHITECTURE.md** | `docs/ARCHITECTURE.md` | runtime stack、dependency graph（Views A/B/C）、llama.cpp taxonomy、evidence ladder |
| **GOVERNANCE.md** | `docs/GOVERNANCE.md` | gate catalogue、ownership rules、claim discipline |
| **VENUS-BRINGUP.md** | `docs/VENUS-BRINGUP.md` | Venus 啟用 step-by-step：host 設定（virglrenderer compile with `-Dvenus=true`）、QEMU config、capset 協商、context 建立、第一次 SUBMIT_3D、ring buffer 設定、troubleshooting |
| **dependency-graph-plan.md** | `docs/dependency-graph-plan.md` | 依賴多圖提取器的設計（跨 Linux/VOGUE/QEMU） |
| **llama-cpp porting plan** | `docs/llama-cpp-unikraft-porting-plan.md` | llama.cpp 到 Unikraft 的 porting 計畫 |
| **venus runtime plan** | `docs/venus-runtime-enablement-plan.md` | Venus runtime enablement：from wire protocol to full Vulkan compute dispatch |
| **VirtIO-GPU spec v1** | `design/unikraft-virtio-gpu-spec-v1.md` | API 規格：feature bits、capset IDs、library boundaries、Out of scope |
| **virtio-gpu-vulken-v1** | `design/virtio-gpu-vulken-v1.md` | research-to-implementation gate ladder、效能評估計畫 |
| **plan-optimize.md** | `plan-optimize.md` | 效能優化計畫（P0-P4）、當前數據基線、優先級分析 |

---

## 五、能否用 Mac 作為 Unikraft/VOGUE 開發機？

### 結論：可以做「開發」，但無法做「GPU 評測」

#### ✅ Mac 上能做的

| 功能 | 方法 |
|------|------|
| 安裝 KraftKit | `brew install unikraft/tap/kraftkit` |
| 建構 unikernel image | `kraft build`（推薦容器化建構，透過 Docker） |
| 跑 host-native 測試（**本專案的 CI gate**） | `make test-fast` / `make native-tests`（不需 GPU/QEMU） |
| 跨架構編譯 x86_64 target | `brew install x86_64-elf-binutils x86_64-elf-gcc` |
| 修改 library 原始碼、撰寫測試 | 完全沒問題 |
| Host-native 測試 / docs 檢查 | `make test-fast` |

#### ❌ Mac 上做不到的

| 功能 | 原因 |
|------|------|
| 跑 `virtio-gpu-gl,venus=true` 的 QEMU | Mac 沒有 `/dev/dri/renderD*`（Linux DRI），QEMU 的 EGL headless + virglrenderer 需要 Linux |
| KVM 加速 QEMU | KVM 是 Linux-only。Mac 有 HVF 但 Unikraft QEMU 不完整支援 |
| `make llama-vk-server-run`、`make kmscube-build` 後的 GPU 評測 | 需要 Venus + GPU |
| 跑 `make test-qemu` | 需要 QEMU VirtIO-GPU |

#### 🔧 推薦的 Mac 開發流程

```
Mac 本機 ──修改程式碼──→ make test-fast ──通過──→ git push
                                                     ↓
Linux GPU 機器 ←──SSH──→ make llama-vk-server-run
```

#### Mac 環境安裝步驟

```bash
# 基礎工具
brew install gnu-sed make coreutils m4 gawk grep wget socat

# KraftKit
brew install unikraft/tap/kraftkit

# Cross-compile toolchain（x86_64 target）
brew install x86_64-elf-binutils x86_64-elf-gcc

# QEMU（可以跑不需 GPU 的功能）
brew install qemu

# 重要：用 gmake 代替 BSD make
alias make=gmake
```

#### 🌐 Mac 開發相關資源

| 資源 | URL |
|------|-----|
| **KraftKit macOS 安裝** | https://unikraft.org/docs/cli/reference/kraftkit |
| **KraftKit GitHub Releases** | https://github.com/unikraft/kraftkit/releases |
| **macOS networking limitations (Issue #1713)** | https://github.com/unikraft/kraftkit/issues/1713 |

---

## 六、推薦學習順序

```
Phase 1: 基礎概念（1-2 天）
  1. Unikraft 論文 (ATC'21)     → 理解 unikernel 和 Library OS 設計理念
  2. Unikraft 架構文件          → micro-library、Kconfig、平台抽象
  3. Vulkan Tutorial (前幾章)    → instance、device、queue、command buffer 基本概念
  4. VirtIO Wiki (OSDev)        → virtqueue 機制

Phase 2: 核心技術（2-3 天）
  5. VirtIO spec §5.7           → VirtIO-GPU 命令格式、feature bits
  6. Collabora Venus 文章       → Venus 架構、wire format、ring buffer
  7. QEMU VirtIO-GPU 文件       → 設備選項、egl-headless

Phase 3: 專案原始碼（2-3 天）
  8. docs/ARCHITECTURE.md       → 本專案架構總覽、dependency graph
  9. docs/VENUS-BRINGUP.md      → Venus 啟用流程
 10. libs/libukvirtio_gpu/      → VirtIO-GPU 前端驅動（從 virtio_gpu_proto.h 開始）
 11. libs/libukvulkan_venus/    → Venus 驅動（從 venus_driver.c → venus_cs.c → venus_compute.c）
 12. libs/libvulkan/            → Vulkan dispatch

Phase 4: 效能與優化（1 天）
 13. plan-optimize.md           → P0-P4 優化方向
 14. README.md 效能表格          → 三環境基準比較
 15. tests/venus_hotpath_test.c → 傳輸優化的量化驗證
```

---

## 七、所有外部 sibling 目錄

| 路徑 | 用途 | 對應資源 |
|------|------|---------|
| `../unikraft` | Unikraft 核心（branch `stable`, commit `7351f8b`） | https://github.com/unikraft/unikraft |
| `../llama.cpp` | upstream llama.cpp（完全不修改） | https://github.com/ggml-org/llama.cpp |
| `../venus-protocol` | Venus 命令集定義，codegen 來源 | https://gitlab.freedesktop.org/vn/venus-protocol |
| `../lib-musl` | musl libc | https://github.com/unikraft/lib-musl |
| `../lib-lwip` | lwIP TCP/IP stack | https://github.com/unikraft/lib-lwip |
| `../qemu-src` | 自建 QEMU（支援 Venus） | https://github.com/qemu/qemu |
| `../Vulkan-Headers` | Vulkan header files | https://github.com/KhronosGroup/Vulkan-Headers |
| `../SPIRV-Headers` | SPIR-V header files | https://github.com/KhronosGroup/SPIRV-Headers |
| `../models` | GGUF 模型檔（Qwen3-0.6B 等） | — |
| `../manifest` | 重現性 metadata | — |

設定在 `config/deps.json`，可用環境變數覆蓋（`LLAMA_ROOT`, `VENUS_PROTOCOL_ROOT`, `VK_INC`, `VK_LIB`）。
