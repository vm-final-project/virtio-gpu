# VOGUE 投影片詳細筆記

---

## 術語背景說明

---

### Vulkan

Vulkan 是 Khronos Group 制定的低階、跨平台 GPU API（2016 年發布），設計目標是取代 OpenGL，給開發者對 GPU 更直接的控制權。

**和 OpenGL 的差異：**
- OpenGL 是高階 API，驅動自動管理很多狀態（如記憶體、同步），開發容易但效能不可預測
- Vulkan 是低階 API，開發者手動管理記憶體配置、command buffer 錄製、synchronization primitive（fence、semaphore、barrier）
- 更明確的控制 → 更低的 CPU overhead，更適合 AI inference 這種需要精確控制 GPU 的場合

**為什麼 llama.cpp 用 Vulkan：**
- llama.cpp 的 ggml-vulkan backend 把矩陣運算（matrix multiply, attention）打包成 GPU compute shader
- Vulkan 的 compute pipeline 不需要顯示器，只需要 compute queue，適合純 GPU 推論
- 跨平台（Linux / Windows / macOS Metal via MoltenVK）

---

### Mesa

Mesa 是 Linux 上最主流的開源圖形驅動套件，實作 OpenGL、Vulkan 等 API。

**在 Linux VM 中的角色：**
- Mesa 提供 guest 端的 Vulkan 驅動。對 virtio-gpu 設備，Mesa 裡有一個叫 **Venus guest driver** 的模組（`src/virtio/vulkan/`）
- Venus guest driver 把 Vulkan API 呼叫序列化成 Venus 協議的 binary stream，再透過 Linux virtio-gpu kernel driver 送給 QEMU

**和 VOGUE 的關係：**
- VOGUE 的 `libukvenus` 實作的是**同一套 Venus 協議**，邏輯等價於 Mesa 的 Venus guest driver
- VOGUE 沒有 Mesa，但透過 `libukvenus` 做一樣的序列化工作
- 找 Venus bug 時靠比對 Mesa 原始碼（`vn_encode_array_size()`）才找到問題

---

### Venus

Venus 是 Mesa 和 virglrenderer 共同定義的 **Vulkan 虛擬化協議**（capset_id=4）。

**核心機制：**

```
Guest (libukvenus / Mesa Venus driver)
  把 vkCreateDevice(), vkQueueSubmit() 等 Vulkan 呼叫
  → 序列化成 PACKED binary stream（無 a
  lignment padding）
  → 透過 VirtIO-GPU 的 SUBMIT_3D 命令送出

Host (virglrenderer)
  接收 binary stream
  → 解碼 Venus 命令
  → 呼叫 host 端真實的 Vulkan driver 執行
```

**PACKED 格式：**
- 欄位之間沒有 alignment padding（不像一般 C struct）
- pointer 的存在性 = uint64（8 bytes）；array 的存在性 = uint32（4 bytes）
- 必須嚴格手動序列化，用錯大小 host 就解析不了，且不會回報錯誤

**為什麼需要 Venus：**
- GPU 指令（Vulkan）本來是直接送給硬體的
- 在 VM 裡，guest 看不到真實 GPU，要把 Vulkan 呼叫「翻譯」成可以傳輸的格式，送給 host 端再執行
- Venus 就是這個翻譯格式的規範

---

## Section: System Design（系統設計）

---

### 投影片 1：Key Principle — Single-Purpose, Single-Client GPU Driver

**核心洞察**

Unikraft 是一個 unikernel 框架：整個系統只有一個 process、一個 address space、一個目的。這個特性帶來一個關鍵推論——GPU 設備永遠只有一個客戶端。

在 Linux 系統中，GPU 是一個共享資源：多個 process 可能同時使用，所以需要 DRM（Direct Rendering Manager）子系統做多工仲裁。DRM 處理 context switching、資源鎖定、permission 管理，是一套非常複雜的基礎設施。但這些在 unikernel 中根本不需要。

**設計決策**

把 DRM 的多工仲裁邏輯整體丟掉，改用一個「最小化的獨佔客戶端驅動」。

**量化對比（表格）**

| 維度 | Linux virtio-gpu | VOGUE |
|------|-----------------|-------|
| 圖形棧大小 | ~3,000,000 LoC | ~5,000 LoC |
| Address space | Kernel + User 分離 | 單一 address space |
| GPU 客戶端 | 多個 process，需要仲裁者 | 獨佔單一客戶端 |
| 命令路徑 | app → ioctl → kernel → virtio | app → 直接存取 virtio ring |

圖形棧從三百萬行縮小到五千行（600×），這是因為去掉了所有多工邏輯、DRM kernel 子系統、以及 user-space 的 Mesa DRM 後端。

**三個解鎖的優點**

1. **沒有 DRM 多工邏輯** → 驅動只需 ~1,650 LoC，邏輯清晰、可審計
2. **沒有 user/kernel 邊界** → 命令提交不需要 ioctl，省掉一次 syscall 的 context switch overhead
3. **直接存取 VirtIO ring** → 命令提交延遲最低

---

### 投影片 2：VOGUE System Architecture

**四層架構（由上到下）**

**L1 — Application**

> **定義**：上游應用程式原始碼，**不修改一行**，直接編譯進 unikernel image。

| 元件 | 說明 |
|------|------|
| kmscube / glmark2 / llama.cpp | upstream source，直接 link |

- **向上暴露**：無（最頂層）
- **向下使用**：EGL/GLES2 API（2D display）、Vulkan API（Vulkan compute）、virgl_encoder API（3D virgl，直接 include L4 header）
- **路徑覆蓋**：全部三條路徑

---

**L2 — Compat**

> **定義**：API 相容層，提供 app 在 Linux 上期待的 API surface，把這些呼叫接進 VOGUE 的實作。**不同路徑的元件完全不同；3D virgl 路徑不走這層。**

| 元件 | 路徑 | 說明 |
|------|------|------|
| `libukegl` | 2D display | EGL/GLES2/GBM/DRM API stub。`eglSwapBuffers` 內部呼叫 libukswrender 渲染，再發 L4 的 2D VirtIO 命令。 |
| `libukswrender` | 2D display | 264 LoC CPU rasterizer，把幾何填成像素寫入 DMA buffer；libukegl 的 render backend。 |
| `libukggml_vk` | Vulkan compute | 靜態 Vulkan dispatch table（80+ stubs），取代 Linux Vulkan loader 的 runtime dlopen ICD，讓 llama.cpp 直接路由到 L3。 |

- **向上暴露**：EGL/GLES2/GBM/DRM API（給 2D display app）；Vulkan API（給 compute app）
- **向下使用**：2D path → L4 的 TRANSFER_TO_HOST_2D / SET_SCANOUT / RESOURCE_FLUSH；Vulkan compute path → L3 的 Venus encoder API
- **路徑覆蓋**：2D display ✓、Vulkan compute ✓、3D virgl **✗**（run_virgl_path 直接呼叫 L4 的 virgl_encoder.h，libukegl 只是 compile-time ABI stub）

---

**L3 — Venus**

> **定義**：Vulkan 命令序列化層，把 Vulkan API 呼叫的參數轉成 Venus PACKED binary stream（無 inter-field alignment padding）。**只存在於 Vulkan compute 路徑。**

| 元件 | 說明 |
|------|------|
| `libukvenus` | 序列化器，邏輯等價於 Mesa Venus guest driver（`src/virtio/vulkan/`）。把 `vkCreateDevice()`、`vkQueueSubmit()` 等的參數寫進 ring buffer，供 virglrenderer 解碼執行。 |

- **向上暴露**：Vulkan API（接受 L2 libukggml_vk 發出的呼叫）
- **向下使用**：向 L4 提交 Venus binary payload，觸發 SUBMIT_3D
- **路徑覆蓋**：Vulkan compute ✓、2D display **✗**、3D virgl **✗**（virgl encoding 在 L4 的 virgl_encoder.c 完成，不走 L3）

---

**L4 — VirtIO**

> **定義**：VirtIO-GPU 協議的 guest 端實作，把各路徑的命令打包成 VirtIO-GPU wire format 送給 QEMU。三條路徑的**唯一交匯點**。virtqueue（descriptor ring 讀寫、doorbell 通知、PCI BAR mapping）是其內部傳輸機制（透過 `LIBVIRTIO_BUS` 引入），不另立層。

| 元件 | 說明 |
|------|------|
| `libukvirtio_gpu` | 核心驅動，約 1,650 LoC。2D 命令：RESOURCE_CREATE_2D、TRANSFER_TO_HOST_2D、SET_SCANOUT、RESOURCE_FLUSH。3D/Venus 命令：CTX_CREATE、SUBMIT_3D、RESOURCE_CREATE_BLOB、RESOURCE_MAP_BLOB。virgl encoding：virgl_encoder.c（Gallium command stream，供 3D virgl path 直接呼叫）。 |
| `libukdma` | DMA 記憶體管理，配置 guest 端 DMA-capable 實體記憶體供 VirtIO-GPU backing store 使用。 |

- **向上暴露**：2D display API（TRANSFER/SET_SCANOUT/FLUSH）；SUBMIT_3D API（給 L3 Venus payload 和 3D virgl path）；virgl_encoder API（給 3D virgl path 直接 include）
- **向下使用**：Unikraft virtqueue → QEMU `virtio-gpu-gl-pci` → virglrenderer
- **路徑覆蓋**：全部三條路徑（交匯點）

**Host 端路徑**

Guest 的 VirtIO-GPU 命令透過 QEMU 的 `virtio-gpu-gl-pci` 設備傳遞到 `virglrenderer`，virglrenderer 再呼叫 host 端的 Vulkan 或 OpenGL 驅動執行實際的 GPU 運算。

**量化效果**

- **Guest image size**: 292 KB（Linux kernel 是 9.2 MB，小 31 倍）
  - 小是因為沒有 kernel 模組系統、沒有 DRM 子系統、沒有多餘的 userspace 庫
- **Boot time**: 10–11 ms（Linux + initramfs 需要 857–861 ms，快 80 倍）
  - 快是因為沒有 init process、沒有設備枚舉的等待、直接跳到應用程式入口

---

### 投影片 3：Three Execution Paths

**三條路徑對照**

| | 2D Display | 3D Rendering | Vulkan Compute |
|--|-----------|--------------|----------------|
| App (L1) | kmscube SW / glmark2 | kmscube virgl | llama.cpp |
| Compat (L2) | libukswrender（CPU render） | —（直接呼 virgl_encoder） | libukggml_vk（static Vk dispatch） |
| Venus (L3) | — | — | libukvenus（Venus binary） |
| VirtIO (L4) | TRANSFER / SET_SCANOUT / FLUSH | SUBMIT_3D (virgl) | SUBMIT_3D (Venus) |
| GPU | 無（CPU only） | host GPU → buffer | host GPU → buffer |

**各路徑的 L2 元件**

- **2D display**：libukswrender 直接被 run_swrender_path 呼叫作為 CPU renderer；libukegl 是 glmark2 的 EGL API surface（其 eglSwapBuffers 內部呼叫 libukswrender 再走 libukvirtio_gpu 2D 命令）。
- **3D virgl**：L2 為空。run_virgl_path 直接 include `<uk/virgl_encoder.h>`（來自 libukvirtio_gpu），libukegl 只是 compile-time ABI 相容層，不在 virgl runtime call chain 上。
- **Vulkan compute**：libukggml_vk 提供靜態 Vulkan dispatch table，是 llama.cpp 和 libukvenus 之間真實存在的一層。app-llama-upstream-vk 的 Config.uk select `LIBUKGGML_VULKAN`，不 select `LIBUKEGL`。

**virgl encoding 在哪裡（已核實 codebase）**

virgl encoding **不在 app，也不在 L3，而在 L4**：

- `libs/libukvirtio_gpu/virgl_encoder.c`：最小化的 virgl command stream encoder，實作 SURFACE、SET_FRAMEBUFFER_STATE、CLEAR 等 virgl 命令的序列化
- kmscube 透過 `#include <uk/virgl_encoder.h>` 使用它，所以 virgl encoding 是 L4（libukvirtio_gpu）的功能，不是 app 自帶

**3D virgl 路徑為何從 L1 直跳 L4（設計理由）**

L2 和 L3 各有獨立的理由，不共用。

---

**為何不走 L2（Compat）**

Compat 層的存在前提是：app 呼叫某個公開 API，Compat 層站在那個邊界攔截並轉接。Vulkan 符合這個條件（`vkCreateDevice`、`vkQueueSubmit` 等 function 邊界讓 libukggml_vk 可以攔截）。

virgl 不符合：Gallium command stream 是 Mesa 的**內部概念**，不是公開 API。`run_virgl_path` 的 app 本身就直接產生 Gallium binary——它呼叫的是 `uk_virgl_encode_create_surface()`、`uk_virgl_encode_clear()` 這些 virgl_encoder.h 裡的函數，而這個 header 屬於 L4（libukvirtio_gpu）。沒有「virgl API 邊界」讓 Compat 層站在那裡，所以 L2 對這條路徑來說根本不存在。

---

**為何不走 L3（Venus）**

兩個獨立的理由：

1. **協議不同**：L3 的 libukvenus 只處理 Vulkan → Venus binary 的序列化。virgl 走的是 Gallium 協議（`virgl_protocol.h`），和 Venus 完全不同，libukvenus 對 virgl 毫無用處。

2. **encoding 規模不需要獨立的層**：`virgl_encoder.c` 只實作 3 個命令（`create_surface`、`set_framebuffer`、`clear`），共約 80 行，目的是讓 kmscube 填一個顏色到螢幕。完整的 virgl encoder 需要覆蓋整個 OpenGL 狀態機（Mesa 的 virgl encoder 有幾千行），直接違反 DG2（Small trusted substrate）。這 3 個命令的體積放在 L4 的 libukvirtio_gpu 裡完全合理，不需要為它建立獨立的 L3。

   evidence 目標也印證了這個規模判斷：`gfx.kmscube.submit` 和 `gfx.kmscube.frame` 的目標只是證明 SUBMIT_3D 傳輸通道能用（`PORTING.md`：「transport-level proof... Full virgl/GLES acceleration is not implied」），最小化的 encoder 在 L4 就已足夠。

> **future work**：如果未來要支援完整 OpenGL GPU 渲染，才需要在 L3 加完整的 virgl serializer。

**依賴關係（已核實）**

| App | 使用的 libs |
|-----|------------|
| kmscube | LIBUKSWRENDER + LIBUKEGL + LIBUKVIRTIO_GPU + LIBUKVENUS |
| llama-upstream-vk | LIBUKGGML_VULKAN（→ LIBUKVENUS + LIBUKVIRTIO_GPU） |

- llama.cpp **不** select LIBUKEGL（不需要顯示 API）
- libukggml_vk 會 select LIBUKVENUS，所以 llama.cpp 透過 L3 走 Venus 路徑

---

### 投影片 4：VOGUE vs Linux: Layer by Layer

**核心訊息**：每一層 VOGUE 元件在 Linux 上都有對應物——有些邏輯等價（≡），有些用更精簡的方式替換（→）。

**四層對照表**

| 層號 | Linux VM | 關係 | VOGUE Unikraft |
|------|----------|------|----------------|
| L1 App | app（kmscube / llama.cpp） | ≡ | app（原始碼不修改） |
| L2 Compat | libEGL + libgbm + libdrm + Vulkan loader | → | libukegl · libukswrender · libukggml_vk |
| L3 Venus | Mesa Venus guest driver | → | libukvenus |
| L4 VirtIO | Linux virtio-gpu driver（/dev/dri ioctl → kernel → virtqueue） | → | libukvirtio_gpu（直接操作 Unikraft virtqueue，無 ioctl 層） |
| Host | — | 共享 | QEMU → virglrenderer → Host GPU driver |

**各層說明**

- **L1**：App 完全不動，是整個設計的起點和終點。
- **L2 Compat**：Linux 有真實的 libEGL/libgbm/libdrm，Vulkan app 透過 Vulkan loader（runtime dlopen ICD）呼叫。VOGUE 改用 stub 實作（libukegl/libukswrender）和靜態 dispatch table（libukggml_vk），省掉動態載入。
- **L3 Venus**：Linux 的 Mesa Venus guest driver 把 Vulkan 呼叫序列化成 Venus binary；VOGUE 的 libukvenus 邏輯等價，只是不依賴 Mesa 其餘龐大的部分。
- **L4 VirtIO**：Linux 透過 `/dev/dri` ioctl 進 kernel，再到 virtqueue；VOGUE 直接操作 Unikraft virtqueue，省掉整個 ioctl 路徑。virtqueue 本身是 libukvirtio_gpu 的內部機制（透過 `LIBVIRTIO_BUS` 拉進來），不另立一層。
- **Host 端**：QEMU、virglrenderer、host GPU driver 完全不動——這就是為什麼 VOGUE 能和現有 hypervisor 直接相容。

---

### 投影片 4：Design Goals

**DG1 — Standards-based**

嚴格遵循 OASIS VirtIO-GPU 規範，確保與 QEMU、crosvm、cloud-hypervisor 等 hypervisor 相容。不自定義私有協議。

**DG2 — Small Trusted Substrate**

整個 guest 圖形棧約 5,000 LoC，小到可以被完整審計（auditable）。這對安全敏感的 unikernel 部署很重要——你需要知道 TCB（Trusted Computing Base）裡有什麼。

**DG3 — Application Source Compatibility**

kmscube、llama.cpp 等應用程式的原始碼**完全不修改**。這代表我們的 compat shim 必須完整模擬這些應用程式所需的 API surface（EGL、GLES2、GBM、DRM）。這很有挑戰性，因為這些 API 在 Linux 上有複雜的狀態機。

**DG4 — Progressive Validation（漸進式驗證）**

驗證策略是由底往上：
1. Native tests（在 host 上直接測試驅動邏輯，不需要 QEMU）
2. SW render（用 CPU rasterizer 驗證 2D 管線）
3. ABI（靜態檢查 Venus wire format 是否正確）
4. QEMU transport（完整的 VirtIO-GPU 命令傳輸）
5. Accelerated render（virgl / Venus GPU 加速）

每一層都有對應的 evidence row，只有通過才能往上。

**DG5 — Evidence-Gated Claims（證據閘門）**

這是方法論上最重要的貢獻。我們嚴格區分不同抽象層次的宣告：

- `gfx.kmscube.sw` PASS（軟體渲染通過）≠ virgl/GPU 加速能工作
- `proto.real-driver` PASS（ABI 靜態檢查通過）≠ QEMU 實際執行能工作
- Host Linux baseline 效能數字 ≠ Unikraft 執行期的效能證據

這樣做的目的是避免「測試 A 通過就宣稱 B 也可以」的常見錯誤。每個宣告都要有對應層次的直接證據。

**現況**：27 個 evidence row，18 個 PASS，9 個 blocked（每個都有明確的阻塞原因和解鎖路徑）。Blocked 不代表失敗，代表誠實地記錄了已知的限制。

---

## Section: Implementation I — Driver Stack（實作）

**敘事弧**：蓋了什麼 → 先驗證簡單路徑 → 再啟用 GPU → 實作規範有多難

各投影片右下角有層次指示圖（黃色 = 當前投影片聚焦的層）。

```
┌──────────────────────────────────────────────────────────────────┐
│  L1 App      kmscube / glmark2 / llama.cpp                       │
├──────────────────────────────────────────────────────────────────┤
│  L2 Compat   libukegl · libukswrender (2D)                       │
│              libukggml_vk              (Vulkan compute)          │
│              —                         (3D virgl: 直接走 L4)     │
├──────────────────────────────────────────────────────────────────┤
│  L3 Venus    libukvenus                (Vulkan compute only)     │
├──────────────────────────────────────────────────────────────────┤
│  L4 VirtIO   libukvirtio_gpu + libukdma                          │
│              (→ Unikraft virtqueue → QEMU，內部機制不另立層)     │
└──────────────────────────────────────────────────────────────────┘
         ↕ QEMU virtio-gpu-gl-pci ↕
         virglrenderer → Host GPU driver
```

---

### 投影片 5：VirtIO-GPU — The Core Driver

**聚焦層**：L4 VirtIO

**核心訊息**：libukvirtio_gpu 實作完整的 VirtIO-GPU 協議（~1,650 LoC）。因為 unikernel 只有一個 GPU 客戶端，不需要 DRM 多工邏輯。

**2D 顯示路徑（繞過 L3 Venus）**

軟體渲染完全不涉及 GPU：

```
CPU rasterizer  → 畫像素到記憶體（libukswrender，264 LoC，shim 的一部分）
L3 Venus        → 跳過（不需要 GPU 命令序列化）
L4 VirtIO       → libukvirtio_gpu：VirtIO-GPU 2D 命令推送給 QEMU → 顯示
```

L2 Compat 在這頁不是重點——它的主要職責是為各路徑提供 API 相容層（2D 用 libukegl/libukswrender、Vulkan 用 libukggml_vk）。這張投影片聚焦在 L4 的 VirtIO-GPU 命令流程。


**Init（只做一次）**

| API | 作用 |
|-----|------|
| `RESOURCE_CREATE_2D` | 告訴 QEMU 建立一個 2D 資源，指定解析度和 pixel 格式（如 RGBX8888） |
| `RESOURCE_ATTACH_BACKING` | 把 guest DMA pages 的實體位址列表送給 QEMU，之後 guest 寫進去 QEMU 就能直接讀 |

實作在 `libukvirtio_gpu`（[apps/virtio-gpu/](../apps/virtio-gpu/)），透過 virtqueue 送出這兩條命令後，guest 和 host 就共享了這塊記憶體。

**Per frame（每幀重複）**

| 步驟 | API | 作用 |
|------|-----|------|
| 1 | *(CPU)* | `libukswrender` 把像素填進 DMA buffer（純 CPU，無 GPU） |
| 2 | `TRANSFER_TO_HOST_2D` | 通知 QEMU 讀取 guest 記憶體並更新 host 端的 2D resource |
| 3 | `SET_SCANOUT` | 把這個 resource 綁定到虛擬顯示器 output |
| 4 | `RESOURCE_FLUSH` | 要求 QEMU 把 resource 的內容輸出到畫面 |
| 5 | Fence wait | 等 QEMU 完成本幀後才開始下一幀（否則 CPU 會覆蓋 QEMU 還在讀的 buffer） |

**Fence 機制**：guest 透過 virtqueue 送出一個帶有唯一 ID 的 fence 命令；QEMU 按命令順序執行完後，把該 fence ID 寫回 guest 可讀的記憶體位置；guest 輪詢（spin-poll）直到看到這個 ID 出現，才確認命令完成。

**Fence 優化**：VirtIO-GPU 規範明確規定設備按命令提交順序處理（in-order）。

```
Naive:     TRANSFER → 送 fence₁ → 等 fence₁ → FLUSH → 送 fence₂ → 等 fence₂
Optimized: TRANSFER → FLUSH → 送 fence → 等 fence
```

等到 FLUSH 的 fence 觸發，代表 FLUSH 已完成；而 TRANSFER 在 FLUSH 之前提交，按順序一定已完成。所以 TRANSFER 不需要自己的 fence，每幀省掉一次 round-trip 等待。

**結果**：kmscube ~484 fps · glmark2 scene-clear ✓

---

### 投影片 6：VirtIO-GPU Venus — GPU Acceleration

**聚焦層**：L3 Venus + L4 VirtIO

**核心訊息**：Venus 是 VirtIO-GPU 的 GPU 加速擴展，把 Vulkan API 呼叫序列化成 binary stream，透過 `SUBMIT_3D` 送到 host 端的 virglrenderer 執行。

**Venus 是什麼**

Venus 是 Mesa/virglrenderer 定義的 Vulkan 序列化協議（capset_id=4）。Guest 端的 `libukvenus` 把每個 Vulkan API 呼叫的參數，按 PACKED 格式（無 alignment padding）序列化成 binary stream，透過 VirtIO-GPU 的 `SUBMIT_3D` 命令送到 host 端的 virglrenderer 解碼並執行。

**兩個關鍵機制**

**Shared Memory — Blob Resource（L4）**

| API | 作用 |
|-----|------|
| `RESOURCE_CREATE_BLOB` | 配置一塊 host-visible 記憶體（類型 `HOST3D_GUEST`） |
| `RESOURCE_MAP_BLOB` | 透過 PCI SHM BAR 把這塊記憶體 map 到 guest address space |
| `RESOURCE_UNMAP_BLOB` | 釋放前解除 mapping |

一般傳輸需要複製（guest 寫 → DMA copy → host 讀）。Blob 讓 guest 直接取得指標存取共享記憶體，guest 寫進去 host 立刻看得到，零複製。這塊共享記憶體就是 Venus ring buffer 的底層存儲。

**Venus Ring Buffer（L3）**

Ring buffer 建在 blob resource 上，佈局如下：

```
Offset   0:  head   (u32) ← host 寫，代表已消費到的位置
Offset  64:  tail   (u32) ← guest 寫，代表新命令的結尾
Offset 128:  status (u32) ← 雙向狀態旗標
Offset 192:  data[] ← 環形命令資料區（power-of-2 大小）
```

每個控制欄位間隔 64 bytes = 一個 CPU cache line，讓 guest 更新 tail 和 host 更新 head 不會互相干擾（避免 false sharing）。

| API | 作用 |
|-----|------|
| `vkCreateRingMESA` (cmd 188) | 向 host 註冊 ring（ring_id、blob handle、size） |
| `vkNotifyRingMESA` (cmd 190) | 觸發 `SUBMIT_3D`，通知 virglrenderer 有新命令 |
| `vkDestroyRingMESA` (cmd 189) | 解除 ring 註冊 |

每次 `SUBMIT_3D` 都是一次完整的 virtqueue 操作（寫 descriptor、敲 doorbell、等回收），代價高。Ring buffer 讓 guest 把多條序列化的 Vulkan 命令連續寫進 `data[]`，更新 tail（store-release，確保命令資料先於 tail 對 host 可見），才呼叫 `vkNotifyRingMESA` 觸發一次 `SUBMIT_3D`。Guest 之後輪詢 head 直到 host 追上（最多 1000 次迭代），確認批次處理完成。

**Fake Backend**：整個 Venus 協議邏輯用純軟體的 fake backend 驗證，不需要 QEMU。Fake backend 模擬 virglrenderer 的回應行為，讓協議正確性和 QEMU 整合分開測試。

**結果**：virgl 像素正確幀 ✓ · llama.cpp GPU path 160 tok/s ✓

---
