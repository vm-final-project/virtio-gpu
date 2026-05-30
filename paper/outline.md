## Recommended SOSP-Style Paper Title

**UnikraftGL: Standards-Based VirtIO Graphics Acceleration for Specialized Unikernels**

Alternative titles:

1. **Bringing Accelerated Graphics to Unikernels with VirtIO-GPU**
2. **A Minimal VirtIO-GPU Stack for Interactive 3D Applications in Unikraft**
3. **From Scanout to Games: VirtIO-GPU Acceleration for Specialized Unikernels**

For SOSP submission, use an anonymized system name if needed. SOSP 2026 uses double-blind reviewing and requires anonymization of project or system names that could identify the authors. ([ACM SIGOPS][1])

---

## SOSP Format Constraints

Use the SOSP/ACM systems-paper style:

| Item            | Requirement                                                |
| --------------- | ---------------------------------------------------------- |
| Page limit      | **12 pages of technical content**, references excluded     |
| Layout          | Two columns                                                |
| Font            | 10 pt font on 12 pt leading                                |
| Paper block     | 178 mm × 229 mm / 7 in × 9 in                              |
| References      | Hyperlinked; not counted in technical page limit           |
| Figures         | Must be readable without magnification and in grayscale    |
| Submission mode | PDF                                                        |
| LaTeX class     | `\documentclass[sigplan,10pt]{acmart}` recommended by SOSP |

SOSP explicitly says submissions are judged on novelty, significance, clarity, correctness, implementation, practicality, and clear advancement beyond prior work. ([ACM SIGOPS][1])

---

# Paper Structure and Section Outline

## Abstract — 0.25 page

**Purpose:** State the problem, core idea, implementation, and results.

**Outline:**

* Unikernels deliver small, specialized VMs, but currently lack practical accelerated graphics support.
* Existing GPU stacks assume Linux DRM/KMS/GBM/Mesa infrastructure, which conflicts with Unikraft’s minimal-library OS model.
* Present **UnikraftGL**, a standards-based VirtIO-GPU/VirtIO-GPU-GL frontend and graphics runtime for Unikraft.
* Demonstrate three milestones:

  * `kmscube`: minimal EGL/GLES scanout.
  * `glmark2-es2-drm`: quantitative OpenGL ES benchmark.
* Summarize headline results: image size, boot time, memory footprint, FPS, glmark score, CPU overhead, and comparison against Linux guest.

Do **not** overclaim. The abstract should say this is a graphics substrate for **accelerated visual workloads**, not a general replacement for Linux GPU stacks.

---

## 1. Introduction — 1.25 pages

**Purpose:** Establish why this is an SOSP-level systems problem.

**Core argument:**

Unikernels have traditionally focused on network, storage, and server workloads. However, modern edge, visualization, UI, game-streaming, browser, emulator, and ML-adjacent workloads increasingly need GPU/display acceleration. The gap is that full Linux graphics stacks are too heavy for a specialized unikernel.

Unikraft’s original design emphasizes modularity, specialization, low image size, low memory use, and low boot time; the EuroSys paper reports roughly 1 MB images for evaluated applications, less than 10 MB RAM, and 3–40 ms total boot time depending on VMM overhead. ([Unikraft][2]) This motivates a graphics stack that preserves those properties.

**Suggested subsections:**

### 1.1 The Case for Graphics-Capable Unikernels

* Current unikernel strengths: fast boot, small footprint, single-purpose deployment.
* Missing capability: accelerated graphics and display output.
* Why this matters:

  * interactive edge VMs,
  * cloud gaming / game streaming,
  * graphical test appliances,
  * embedded visual workloads,
  * GPU-backed UI or visualization services.

### 1.2 Why Existing Linux Graphics Is the Wrong Abstraction

* Linux path: DRM/KMS, GBM, EGL, Mesa, virgl, device nodes, ioctls.
* Unikraft should not clone Linux DRM wholesale.
* The research challenge is to expose enough graphics functionality for real workloads while keeping the guest-side OS stack minimal.

### 1.3 Thesis

Suggested thesis:

> A unikernel can support practical accelerated graphics without importing a full Linux graphics subsystem by implementing a narrow, standards-based VirtIO-GPU frontend, a minimal scanout/runtime layer, and a compatibility path sufficient for EGL/GLES applications.

### 1.4 Contributions

Use 4–5 precise contributions:

1. **A minimal VirtIO-GPU frontend for Unikraft**, supporting 2D scanout, resource management, command submission, fencing, and display configuration.
2. **A VirtIO-GPU-GL acceleration path**, integrating with QEMU’s `virtio-gpu-gl`/VirGL model rather than a custom host protocol.
3. **A lightweight graphics runtime**, sufficient for EGL/GLES applications without importing Linux DRM/KMS as-is.
5. **A detailed performance evaluation** against Linux guests and Unikraft non-accelerated baselines.

---

## 2. Background and Motivation — 1 page

**Purpose:** Explain only the background needed to understand the design.

### 2.1 Unikraft

Explain:

* Unikraft is a micro-library OS.
* It selects only required OS components.
* It targets small, optimized, single-purpose VMs.
* Its architecture makes a monolithic Linux-style graphics stack undesirable.

Cite Unikraft’s modular design and performance goals. ([Unikraft][2])

### 2.2 VirtIO-GPU and VirtIO-GPU-GL

Explain:

* VirtIO provides standard, efficient virtual devices rather than per-OS boutique mechanisms. ([OASIS][3])
* VirtIO-GPU supports both 2D and 3D modes.
* The device exposes `controlq` and `cursorq`.
* Important features:

  * `VIRTIO_GPU_F_VIRGL`
  * `VIRTIO_GPU_F_EDID`
  * `VIRTIO_GPU_F_RESOURCE_BLOB`
  * `VIRTIO_GPU_F_CONTEXT_INIT`
* QEMU’s accelerated backend uses VirGL: guest OpenGL calls are translated into Gallium IR, sent to the host, and translated back into host OpenGL calls by `virglrenderer`. ([GitHub][4])

### 2.3 Target Applications

| Application          | Why It Is in the Paper                                                                                                                                                                         |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `kmscube`            | Minimal proof that KMS/GBM/EGL/GLES-style scanout works. It is specifically a small program for bare-metal graphics without X11/Wayland, using DRM/KMS, GBM, EGL, and OpenGL ES. ([GitHub][5]) |
| `glmark2-es2-drm`    | Quantitative benchmark. It is an OpenGL ES 2.0 benchmark suite and has a DRM backend. ([Ubuntu Manpages][6])                                                                                   |

---

## 3. Design Goals and Challenges — 1 page

**Purpose:** Define what your system must achieve and why it is hard.

### 3.1 Goals

1. **Standards-based device interface**

   * Use VirtIO-GPU and VirtIO-GPU-GL.
   * Avoid bespoke GPU RPC unless absolutely necessary.

2. **Minimal guest-side footprint**

   * No full Linux DRM subsystem.
   * No unnecessary device-manager model.
   * No generic desktop graphics stack.

3. **Application compatibility**

   * Support enough of the DRM/KMS/GBM/EGL/GLES path to run the selected apps.

4. **Interactive performance**

   * Stable frame pacing.
   * Low input-to-display latency.
   * Low CPU overhead in the guest.

5. **Measurable acceleration**

   * `glmark2-es2-drm` should show improvement over software-rendered or non-accelerated baselines.

### 3.2 Non-Goals

Be explicit:

* Not supporting full desktop Linux graphics.
* Not supporting arbitrary Vulkan in the first version.
* Not implementing a complete Linux DRM/KMS clone.
* Not solving GPU passthrough, SR-IOV, or vendor-native GPU drivers.
* Not guaranteeing security isolation equivalent to confidential GPU computing.

### 3.3 Challenges

Suggested challenges:

* **C1: Linux graphics API dependency.** Apps expect `/dev/dri`, ioctls, GBM, EGL.
* **C2: VirtIO-GPU command sequencing.** Resources, attach backing, scanout, transfer, flush, fences.
* **C3: Synchronization.** Fences and frame completion must be correct or games stutter.
* **C4: Memory model mismatch.** VirtIO-GPU resources are host-private unless blob/shared-memory features are used. The spec states that guest data is transferred into host-private resources unless shared memory regions are available. ([GitHub][4])
* **C5: Minimality vs. compatibility.** Supporting real apps without importing Linux wholesale.

---

## 4. System Overview — 1 page

**Purpose:** Give the reader one architectural picture.

### 4.1 Architecture Figure

Include a figure like:

```text
+-------------------------------------------------------------+
| Unikraft Application VM                                     |
|                                                             |
|          |                                                  |
|  Minimal EGL/GLES/GBM/DRM compatibility layer               |
|          |                                                  |
|  Unikraft graphics runtime                                  |
|          |                                                  |
|  ukvirtio-gpu frontend                                      |
|   - PCI/MMIO discovery                                      |
|   - controlq/cursorq                                        |
|   - resource manager                                        |
|   - scanout manager                                         |
|   - fence/sync manager                                      |
|   - virgl context/command path                              |
+----------|--------------------------------------------------+
           |
           | VirtIO queues
           v
+-------------------------------------------------------------+
| QEMU/KVM                                                     |
|  virtio-gpu / virtio-gpu-gl                                  |
|  virglrenderer                                               |
+----------|--------------------------------------------------+
           |
           v
+-------------------------------------------------------------+
| Host GPU / Mesa / OpenGL Driver                              |
+-------------------------------------------------------------+
```

### 4.2 Execution Paths

Describe three paths:

1. **2D scanout path**

   * framebuffer resource creation,
   * attach backing,
   * transfer to host,
   * set scanout,
   * flush.

2. **EGL/GLES path**

   * app creates rendering context,
   * commands are encoded for VirGL,
   * host executes via virglrenderer,
   * rendered output is presented through scanout.

3. **Interactive app path**

   * input events,
   * frame loop,
   * swap/present,
   * fence wait,
   * timing.

---

## 5. VirtIO-GPU Frontend Design — 1.5 pages

**Purpose:** This is the core systems design section.

### 5.1 Device Discovery and Initialization

Cover:

* PCI/MMIO discovery.
* Feature negotiation.
* Virtqueue setup.
* Display info query.
* EDID handling if available.
* Number of scanouts.

VirtIO-GPU initialization should query display information using `GET_DISPLAY_INFO`; EDID can be fetched if `VIRTIO_GPU_F_EDID` is negotiated. ([GitHub][4])

### 5.2 Resource Management

Explain:

* Resource IDs.
* 2D resources.
* Backing pages.
* Guest physical memory mapping.
* Host-visible blob resources as later optimization.

Relevant commands:

* `RESOURCE_CREATE_2D`
* `RESOURCE_ATTACH_BACKING`
* `TRANSFER_TO_HOST_2D`
* `RESOURCE_FLUSH`
* `RESOURCE_UNREF`

### 5.3 Scanout and Display

Explain:

* `SET_SCANOUT`
* display resolution selection,
* fullscreen mode,
* damage/flush regions,
* double/triple buffering.

### 5.4 3D/VirGL Command Path

Explain:

* feature negotiation for `VIRTIO_GPU_F_VIRGL`,
* context creation,
* capset query,
* command submission,
* resource lifecycle for 3D buffers,
* host rendering through VirGL.

QEMU documents `virtio-gpu-gl` as the VirGL accelerated graphics mode. ([QEMU 文檔][8])

### 5.5 Fence and Synchronization Design

Explain:

* why fences matter,
* blocking vs. polling wait,
* frame pacing,
* avoiding excessive busy-waiting,
* measuring fence latency.

This section is important for SOSP. A graphics driver that merely “runs” but has poor synchronization is not convincing.

---

## 6. Graphics Runtime and Application Support — 1.25 pages

**Purpose:** Explain how the target applications run without Linux.

### 6.1 Minimal DRM/KMS Compatibility

You need to decide how honest the paper is here. The cleanest framing:

> We do not implement Linux DRM/KMS. We implement a narrow compatibility layer for the subset of DRM/KMS/GBM behavior required by the selected applications.

Describe:

* fake/minimal `/dev/dri/card0` abstraction if needed,
* ioctl subset,
* mode enumeration,
* dumb buffer or GBM-like buffer allocation,
* page-flip emulation,
* event delivery.

### 6.2 EGL/GLES Binding Strategy

Explain one of these strategies:

| Strategy                                      | Paper Framing                                              |
| --------------------------------------------- | ---------------------------------------------------------- |
| Port Mesa subset                              | More compatible but heavier                                |
| Link against a minimized EGL/GLES frontend    | Better Unikraft fit                                        |
| Shim app-level GBM/DRM calls to Unikraft APIs | Best first prototype                                       |
| Direct custom backend for apps                | Acceptable for early implementation, weaker as paper claim |

For SOSP, the stronger claim is: **minimal reusable runtime**, not app-specific hacks.

### 6.3 Application-Specific Porting

Split by app:

#### `kmscube`

* Minimal scanout.
* Verify EGL context creation.
* Verify rendered cube presentation.
* Useful for correctness, not performance.

#### `glmark2-es2-drm`

* Requires EGL/GLESv2 and DRM backend.
* Main benchmark for throughput.
* Report per-scene and total score.


* Real event loop.
* Input, audio optional, display, timing.
* Report FPS stability and latency.

---

## 7. Implementation — 1.25 pages

**Purpose:** Make the work concrete.

### 7.1 Code Organization

Suggested modules:

```text
lib/ukvirtio_gpu/
  virtio_gpu_device.c
  virtio_gpu_queue.c
  virtio_gpu_cmd.c
  virtio_gpu_resource.c
  virtio_gpu_scanout.c
  virtio_gpu_fence.c
  virtio_gpu_virgl.c

lib/ukgfx/
  uk_gfx_display.c
  uk_gfx_buffer.c
  uk_gfx_context.c
  uk_gfx_present.c

lib/ukdrm_compat/
  drm_ioctl_subset.c
  gbm_compat.c
  kms_compat.c

apps/
  kmscube/
  glmark2/
```

### 7.2 Implementation Scope Table

| ------------------ | ---------------------: | ---------------------: | ---------------------: |
| VirtIO device init |                    Yes |                    Yes |                    Yes |
| 2D resource path   |                    Yes |                    Yes |                    Yes |
| scanout/flush      |                    Yes |                    Yes |                    Yes |
| fences             |                  Basic |                    Yes |                    Yes |
| VirGL context      |                    Yes |                    Yes |                    Yes |
| GBM compatibility  |                    Yes |                    Yes |               Possibly |
| input events       |                     No |                     No |                    Yes |
| audio              |                     No |                     No |               Optional |
| filesystem assets  |                     No |       Textures/shaders |                    Yes |

### 7.3 Engineering Decisions

Explain why you chose:

* VirtIO-GPU over GPU passthrough.
* VirtIO-GPU-GL over vendor GPU drivers.
* A minimal DRM/GBM shim over full Linux DRM.
* QEMU/virglrenderer as the first backend.

---

## 8. Evaluation — 2 pages

**Purpose:** This is the most important SOSP section. The paper must prove usefulness, not just correctness.

### 8.1 Research Questions

Use explicit RQs:

| RQ  | Question                                                         | Workload             |
| --- | ---------------------------------------------------------------- | -------------------- |
| RQ1 | Can Unikraft initialize VirtIO-GPU and present frames correctly? | `kmscube`            |
| RQ2 | Does VirtIO-GPU-GL provide measurable acceleration?              | `glmark2-es2-drm`    |
| RQ4 | What is the cost in image size, memory, boot time, and CPU?      | all                  |
| RQ5 | Where is the overhead relative to Linux guests?                  | all                  |

### 8.2 Experimental Setup

Report:

* Host CPU/GPU.
* Host OS/kernel.
* QEMU version.
* virglrenderer version.
* Mesa version.
* Unikraft commit.
* Linux guest baseline kernel/config.
* QEMU command lines:

  * `-device virtio-gpu`
  * `-device virtio-gpu-gl`
  * `-display egl-headless` or equivalent.
* Resolution: e.g., 800×600, 1280×720, 1920×1080.
* Repeat count and statistical treatment.

### 8.3 Baselines

Minimum baselines:

1. **Linux guest + virtio-gpu-gl**
2. **Linux guest + virtio-gpu 2D + software rendering**
3. **Unikraft + virtio-gpu 2D path**
4. **Unikraft + virtio-gpu-gl path**
5. Optional: native Linux host.

### 8.4 Metrics

| Category      | Metrics                                                                 |
| ------------- | ----------------------------------------------------------------------- |
| Correctness   | successful initialization, rendered frames, visual validation, no hangs |
| Performance   | FPS, frame time, p50/p95/p99 frame latency                              |
| Benchmark     | glmark2 total score and per-scene score                                 |
| CPU overhead  | guest CPU %, host QEMU CPU %, virglrenderer CPU %                       |
| Memory        | guest RSS, unikernel image size, runtime memory                         |
| Boot          | time to first frame, time to app start                                  |
| Interactivity | input-to-photon approximation, frame drops, jitter                      |
| Engineering   | LoC added, app changes, unsupported ioctls                              |

### 8.5 Figures to Include

Use these figures:

1. **Figure 1:** Architecture overview.
2. **Figure 2:** VirtIO-GPU command flow.
3. **Figure 3:** Time-to-first-frame comparison.
4. **Figure 4:** `glmark2-es2-drm` score vs baselines.
6. **Figure 6:** Image size and memory footprint.
7. **Table 1:** Supported commands/features.
8. **Table 2:** Application porting effort.

### 8.6 Expected Evaluation Narrative

The ideal results story:

* `kmscube` proves the scanout and EGL/GLES path.
* `glmark2-es2-drm` shows acceleration is real and quantifiable.
* Unikraft remains much smaller and faster to boot than Linux guests.
* Performance gap to Linux guest is explained by specific overheads: fence waits, command batching, memory copies, or missing blob resources.

---

## 9. Discussion and Limitations — 0.75 page

**Purpose:** Preempt reviewer criticism.

Include:

### 9.1 What This Does Not Yet Support

* Full DRM/KMS API.
* Wayland/X11.
* Vulkan/Venus unless implemented.
* Multiple windows/compositors.
* GPU memory eviction.
* Advanced synchronization timelines.
* Vendor-native GPU features.

### 9.2 Why This Is Still Valuable

Argue:

* The goal is not desktop Linux replacement.
* The goal is a specialized graphics-capable unikernel substrate.
* The three workloads progressively prove:

  * minimal rendering,
  * quantitative acceleration,
  * real interactive usefulness.

### 9.3 Future Work

* Venus/Vulkan path.
* Blob resources and host-visible memory.
* vhost-user-gpu isolation.
* zero-copy frame transfer.
* multi-scanout support.
* integration with graphical ML/edge workloads.

---

## 10. Related Work — 1 page

Group related work by category:

### 10.1 Unikernels and Library OSes

* Unikraft.
* MirageOS.
* IncludeOS.
* OSv.
* Graphene/LibOS-style systems if relevant.

### 10.2 GPU Virtualization

* VirtIO-GPU.
* VirGL.
* Venus.
* Rutabaga/gfxstream.
* vhost-user-gpu.
* GPU passthrough/SR-IOV.

### 10.3 Minimal Graphics Stacks

* DRM/KMS.
* GBM/EGL.
* Mesa/virglrenderer.
* headless rendering systems.

### 10.4 Application Porting to Unikernels

Compare against previous Unikraft application-porting papers and explain why graphics is structurally harder than network/storage apps.

---

## 11. Conclusion — 0.25 page

**Purpose:** Restate the result crisply.

Suggested conclusion:

> This paper shows that accelerated graphics in a unikernel does not require importing a full Linux graphics subsystem. By implementing a standards-based VirtIO-GPU/VirtIO-GPU-GL frontend and a narrow graphics runtime in Unikraft, we run a minimal EGL/GLES scanout demo, a quantitative OpenGL ES benchmark, and a real interactive 3D application. These results suggest that graphics-capable specialized VMs are feasible while preserving the footprint and deployment benefits that motivate unikernels.

---

# Recommended 12-Page Budget

| Section                                     |     Pages |
| ------------------------------------------- | --------: |
| Abstract                                    |      0.25 |
| 1. Introduction                             |      1.25 |
| 2. Background and Motivation                |      1.00 |
| 3. Design Goals and Challenges              |      1.00 |
| 4. System Overview                          |      1.00 |
| 5. VirtIO-GPU Frontend Design               |      1.50 |
| 6. Graphics Runtime and Application Support |      1.25 |
| 7. Implementation                           |      1.25 |
| 8. Evaluation                               |      2.00 |
| 9. Discussion and Limitations               |      0.75 |
| 10. Related Work                            |      0.75 |
| 11. Conclusion                              |      0.25 |
| **Total**                                   | **12.00** |

---

# Best Paper Narrative

Your strongest SOSP framing is:

> **Unikraft historically shows that specialized VMs can outperform Linux guests for server workloads. This paper asks whether the same specialization principle can extend to accelerated graphics. The answer is yes: by implementing the narrow VirtIO-GPU/VirGL substrate needed by real applications, Unikraft can run practical 3D workloads without inheriting the complexity of Linux’s full graphics stack.**

The three applications should be presented as a **progressive validation ladder**:

1. **`kmscube`**: correctness of scanout and EGL/GLES.
2. **`glmark2-es2-drm`**: quantitative accelerated rendering.

[1]: https://sigops.org/s/conferences/sosp/2026/cfp.html "SOSP 2026: The 32nd Symposium on Operating Systems Principles"
[2]: https://unikraft.org/blog/2021-04-26-unikraft-at-eurosys "Unikraft: Fast, Specialized Unikernels the Easy Way - Unikraft"
[3]: https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html "Virtual I/O Device (VIRTIO) Version 1.3"
[4]: https://github.com/oasis-tcs/virtio-spec/blob/master/device-types/gpu/description.tex "virtio-spec/device-types/gpu/description.tex at master · oasis-tcs/virtio-spec · GitHub"
[5]: https://github.com/embtom/kmscube "GitHub - embtom/kmscube · GitHub"
[6]: https://manpages.ubuntu.com/manpages/jammy/man1/glmark2-es2-drm.1.html "Ubuntu Manpage: glmark2-es2-drm - OpenGL (ES) 2.0 benchmark suite"
[8]: https://qemu.readthedocs.io/en/v9.2.4/system/devices/virtio-gpu.html "virtio-gpu — QEMU 9.2.4 documentation"


Yes — add an **Artifact Appendix**, but do **not** rely on it for the core paper argument.

For SOSP, the main 12-page paper must stand alone. SOSP 2026 allows appendices in the camera-ready version, but says appendices are beyond the main page limit, should contain supplementary material, require shepherd approval, and must clearly indicate they were not peer-reviewed. ([ACM SIGOPS][1]) SOSP also has an optional artifact evaluation process after acceptance, with artifact registration and submission after paper notification. ([ACM SIGOPS][2]) ACM artifact badging distinguishes artifact availability, artifact evaluation, and result validation. ([計算機協會][3])

## Where to Place It

Add after references:

```text
References

Appendix A. Artifact Appendix
```

Do **not** include it in the main 12-page technical budget. In the submitted/review version, keep only a short anonymized artifact statement if allowed. In the camera-ready, expand it.

---

# Appendix A. Artifact Appendix

## A.1 Artifact Overview

**Goal:** make the implementation reproducible and reusable.

Include:

* Unikraft fork/branch containing `virtio-gpu` and `virtio-gpu-gl` support.
* Build scripts for:

  * `kmscube`
  * `glmark2-es2-drm`
* QEMU launch scripts.
* Host setup scripts.
* Benchmark collection scripts.
* Raw result logs and plotting scripts.

Suggested text:

> This artifact contains the Unikraft VirtIO-GPU frontend, the minimal graphics compatibility layer, application ports, build scripts, QEMU launch scripts, benchmark harnesses, and raw data used to produce the evaluation figures in the paper.

---

## A.2 Artifact Claims

Map the artifact directly to the paper’s claims.

| Paper Claim                              | Artifact Component    | Validation                     |
| ---------------------------------------- | --------------------- | ------------------------------ |
| Unikraft can initialize VirtIO-GPU       | `ukvirtio_gpu` driver | boot log + display info query  |
| Unikraft can present frames              | `kmscube`             | rendered cube screenshot/video |
| VirtIO-GPU-GL enables acceleration       | `glmark2-es2-drm`     | score and per-scene logs       |
| The stack preserves unikernel minimality | build output          | image size, memory, boot time  |
| Results are reproducible                 | scripts + raw logs    | regenerated figures            |

---

## A.3 Hardware and Software Requirements

For your case, be precise.

```text
Host CPU: x86_64 with KVM support
Host GPU: Intel/AMD/NVIDIA GPU with working OpenGL/EGL stack
Host OS: Linux
VMM: QEMU with virtio-gpu-gl support
Renderer: virglrenderer
Guest OS: Unikraft
```

Also state known constraints:

* `virtio-gpu-gl` requires a working host OpenGL/EGL path.
* NVIDIA may require correct EGL/GBM/render-node setup.
* Results depend on host GPU driver, Mesa/virglrenderer version, and QEMU version.

---

## A.4 Repository Layout

Suggested artifact layout:

```text
artifact/
  README.md
  AE.md
  LICENSE
  scripts/
    setup-host.sh
    build-all.sh
    run-kmscube.sh
    run-glmark2.sh
    collect-results.sh
    plot-results.py

  unikraft/
    lib/ukvirtio_gpu/
    lib/ukgfx/
    lib/ukdrm_compat/

  apps/
    kmscube/
    glmark2/

  qemu/
    qemu-virtio-gpu.sh
    qemu-virtio-gpu-gl.sh
    qemu-egl-headless.sh

  results/
    raw/
    processed/
    figures/

  docs/
    host-setup.md
    troubleshooting.md
    expected-output.md
```

---

## A.5 Build Instructions

Include deterministic commands.

```bash
git clone <artifact-repo>
cd artifact

./scripts/setup-host.sh
./scripts/build-all.sh
```

Then per app:

```bash
./scripts/build-kmscube.sh
./scripts/build-glmark2.sh
```

The artifact should state expected outputs:

```text
build/kmscube/kmscube_unikraft-x86_64
build/glmark2/glmark2_unikraft-x86_64
```

---

## A.6 Running the Experiments

### A.6.1 Correctness: `kmscube`

```bash
./scripts/run-kmscube.sh --gpu virtio-gpu-gl --resolution 1280x720
```

Expected result:

* guest boots,
* VirtIO-GPU device is detected,
* EGL/GLES context is created,
* cube renders,
* frame flushes complete.

Record:

```text
logs/kmscube-boot.log
logs/kmscube-render.log
screenshots/kmscube.png
```

---

### A.6.2 Benchmark: `glmark2-es2-drm`

```bash
./scripts/run-glmark2.sh --gpu virtio-gpu-gl --resolution 1280x720
```

Expected result:

```text
glmark2 Score: <number>
```

Collect:

```text
results/raw/glmark2-unikraft-virtio-gpu-gl.txt
results/raw/glmark2-linux-virtio-gpu-gl.txt
results/raw/glmark2-linux-software.txt
```

---


```bash
```

Expected result:

* game starts,
* menu renders,
* input works,
* timedemo runs,
* FPS trace is produced.

Collect:

```text
```

---

## A.7 Reproducing Paper Figures

Map scripts to figures.

| Paper Figure/Table              | Script              | Output                      |
| ------------------------------- | ------------------- | --------------------------- |
| Figure 3: time to first frame   | `plot-ttff.py`      | `figures/ttff.pdf`          |
| Figure 4: glmark2 score         | `plot-glmark2.py`   | `figures/glmark2.pdf`       |
| Figure 6: image size and memory | `plot-footprint.py` | `figures/footprint.pdf`     |
| Table 2: porting effort         | `count-loc.sh`      | `tables/loc.md`             |

Example command:

```bash
./scripts/collect-results.sh
./scripts/plot-results.py
```

---

## A.8 Expected Time and Resource Cost

Give reviewers realistic estimates.

| Step                         | Expected Time |
| ---------------------------- | ------------: |
| Host dependency installation |     10–30 min |
| Build Unikraft and apps      |     10–20 min |
| Run `kmscube`                |       < 5 min |
| Run `glmark2-es2-drm`        |      5–15 min |
| Regenerate all plots         |       < 5 min |

---

## A.9 Troubleshooting

This is important for `virtio-gpu-gl`.

Include failures like:

| Symptom                             | Likely Cause                 | Fix                                               |
| ----------------------------------- | ---------------------------- | ------------------------------------------------- |
| `egl: no drm render node available` | host render node unavailable | check `/dev/dri/renderD*`, permissions, EGL setup |
| black screen                        | scanout not flushed          | verify `RESOURCE_FLUSH` and fence completion      |
| `virgl` context creation fails      | missing `VIRTIO_GPU_F_VIRGL` | check QEMU command and feature negotiation        |
| low FPS                             | software rendering fallback  | check `glxinfo`, `eglinfo`, Mesa/driver path      |

---

## A.10 Artifact Badges Target

You should target:

| Badge                                |      Target? | Rationale                                       |
| ------------------------------------ | -----------: | ----------------------------------------------- |
| **Artifacts Available**              |          Yes | publish source, scripts, configs, raw results   |
| **Artifacts Evaluated – Functional** |          Yes | reviewers can build and run the three apps      |
| **Artifacts Evaluated – Reusable**   | Stretch goal | requires clean documentation and modular code   |
| **Results Validated / Reproduced**   |       Harder | needs independent reproduction of paper results |

A realistic first target is **Artifacts Available + Artifacts Evaluated Functional**. Reusable is possible only if the code is clean enough for others to port additional Unikraft graphics apps.

---

## Revised Paper Outline with Artifact Appendix

```text
Abstract

1. Introduction
2. Background and Motivation
3. Design Goals and Challenges
4. System Overview
5. VirtIO-GPU Frontend Design
6. Graphics Runtime and Application Support
7. Implementation
8. Evaluation
9. Discussion and Limitations
10. Related Work
11. Conclusion

References

Appendix A. Artifact Appendix
  A.1 Artifact Overview
  A.2 Artifact Claims
  A.3 Hardware and Software Requirements
  A.4 Repository Layout
  A.5 Build Instructions
  A.6 Running the Experiments
  A.7 Reproducing Paper Figures
  A.8 Expected Time and Resource Cost
  A.9 Troubleshooting
  A.10 Artifact Badges Target
```

## Recommendation


[1]: https://sigops.org/s/conferences/sosp/2026/authors.html?utm_source=chatgpt.com "SOSP 2026: The 32nd Symposium ..."
[2]: https://sigops.org/s/conferences/sosp/2026/cfp.html?utm_source=chatgpt.com "SOSP 2026 Call for Papers"
[3]: https://www.acm.org/publications/policies/artifact-review-and-badging-current?utm_source=chatgpt.com "Artifact Review and Badging - Current"
