# VOGUE repository governance

VOGUE is managed as a research-artifact monorepo. Governance is intentionally
stricter than a normal library repo because apps, Unikraft libraries, generated
results, and paper claims can otherwise drift apart.

## Developer workflow split

Use the lightest target that proves the thing being changed:

| Gate | Target | Intended use | Claim power |
|---|---|---|---|
| Fast developer | `make test-fast` | Metadata governance, app/lib docs, host-native tests, protocol ABI. | No performance or QEMU/GPU runtime claim. |
| Native | `make test-native` | Host-native library/application substrate regressions. | Native correctness only. |
| QEMU | `make test-qemu` | VirtIO-GPU/QEMU probes; may return structured blockers on unsuitable hosts. | QEMU transport only when same-run PASS evidence exists. |
| GPU/static | `make test-gpu` | Host Vulkan plus static Venus/ggml-vulkan dispatch checks. | Static/API coverage unless QEMU runtime also passes. |
| Evaluation | `make eval-check` | Regenerate the evidence matrix. | Generated PASS/blocked rows only. |
| Release | `make verify` or `make artifact-full` | Broad artifact gate before promoting paper/README claims. | Release claims only when linked to manifest-backed evidence. |

Existing historical targets remain callable; the new names are reviewer-facing
aliases that make the daily-vs-release split explicit.

## App status policy

Every `apps/*` directory must be listed in `config/governance.json` with one of:

- `canonical`: primary reviewer-facing app or proof path.
- `benchmark`: measurement workload or benchmark harness.
- `experimental`: useful evidence or comparison path, but not canonical.
- `deprecated`: retained for history/comparison only.
- `demo`: bounded proof surface, not a product path.

Top-level README claims should cite canonical or benchmark apps. Experimental,
demo, and deprecated apps need explicit context and must not be silently treated
as release proof.

## Library API policy

Every `libs/*` directory must have:

- an owner/domain in `config/governance.json`,
- API stability classification,
- public header metadata when it exports headers,
- a boundary statement in both metadata and `libs/*/README.md`.

Compatibility shims stay bounded. `libukvirtgpu_drm` must not become full Linux
DRM, `libukvulkan_venus` must not leak into CPU-only paths, and llama/ggml libraries
must not introduce a custom compute-remoting framework.


## Directory ownership

| Directory | Owner role | Must not contain |
|---|---|---|
| `vm-final-project/` (this repo) | Source, local tests, local eval generators, docs, latest evidence. | Manifest-runner `exp-*` scripts, second manifest lock, generated run archives. |
| sibling `../manifest/` | Reproducibility owner: experiment specs, env capture, hashing, validation, reproduce scripts, run archives. | VM source code, app/lib unit tests, VM-local eval/perf generator scripts. |

Reproducibility manifests live in the sibling `../manifest/` checkout; this repo
references them and must not duplicate manifest-owned runner/lock files.

## Enforcement

Run:

```sh
make governance-check
make test-fast
```

The check fails if apps/libs/claims/manifest links are missing required
governance metadata, if the Make target taxonomy drifts, or if VM starts
duplicating manifest-owned runner/lock files.
