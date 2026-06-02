= Abstract

Unikernels deliver small images and fast boot, but graphics and GPU-backed
applications typically rely on the Linux DRM/KMS stack, Mesa, and large guest
driver dependencies. VOGUE asks how much of that stack a Unikraft guest must
actually import to run bounded graphics workloads and upstream `llama.cpp`
through VirtIO-GPU. Our answer is a narrow graphics substrate: a guest-side
VirtIO-GPU frontend, a minimal DRM/GBM/EGL compatibility layer, a software
raster path for correctness, a Mesa-compatible Venus encoder and ring protocol,
and a static Vulkan dispatch layer that lets upstream `ggml-vulkan.cpp` run
without a guest `libvulkan.so`.

The paper's central systems claim is a dependency collapse: Linux reaches the
host renderer through a large DRM/Mesa tower, while VOGUE reaches the same
VirtIO-GPU/Venus protocol seam through a small chain of Unikraft libraries and
explicit compatibility boundaries. We validate that claim with an
evidence-gated methodology. On the evaluation host, the generated matrix is
27/27 PASS. Host-native tests validate protocol ABI, 2D resource sequencing,
Venus encoding and ring behavior, Vulkan ICD bootstrap, and static ggml-Vulkan
dispatch. At runtime, the project demonstrates same-run QEMU/Venus transport,
virgl submit/frame proof for `kmscube`, and upstream `llama.cpp` Vulkan
execution on a Tesla V100. After enabling batched Venus submission and explicit
llama.cpp batch controls, the latest same-run Vulkan appliance reports
`pp512=2232.1 t/s` and `tg128=160.2 t/s`, while the Vulkan server appliance
loads the model over Venus and reaches model-loaded readiness with batching
enabled. VOGUE therefore shows that a unikernel can host a useful graphics and
Vulkan substrate without importing Linux DRM/KMS or Mesa wholesale, while still
keeping unsupported claims explicit as structured blockers rather than implied
success.
