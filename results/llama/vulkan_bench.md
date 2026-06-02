# host.bench.vk — Vulkan/Venus llama.cpp throughput across environments

Generated: `2026-05-28T17:05:38Z`

| Env | Label | Backend | GPU | pp512 t/s | tg128 t/s |
|-----|-------|---------|-----|----------:|----------:|
| `cpu-1t` | Host CPU (1 thread) | ggml-cpu | - | 20590.36 | 725.11 |
| `cpu-32t` | Host CPU (32 threads) | ggml-cpu | - | 66328.38 | 3047.15 |
| `vulkan-gpu` | Host Vulkan (discrete GPU) | ggml-vulkan | NVIDIA RTX 4000 Ada Generation | 213050.36 | 2409.6 |
| `cuda-gpu` | Host CUDA (discrete GPU) | ggml-cuda | NVIDIA RTX 4000 Ada Generation | 232752.06 | 4472.76 |

**Vulkan/GPU vs CPU/1T**: pp512 ×10.35, tg128 ×3.32.  
**Vulkan/GPU vs CUDA/GPU**: tg128 ratio = 0.54 (<1.0 means CUDA leads, expected for NVIDIA hardware).

Model: `tiny-random-LlamaForCausalLM-Q2_K.gguf` (4.12 M params, 4.17 MiB). Same upstream llama.cpp commit and same binary set used by `apps/app-llama-vulkan`.
