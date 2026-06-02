# Test spec: virtio-gpu paper SOSP refresh

## Verification targets
1. Structural/documentation checks
   - `python3 scripts/paper_consistency_check.py`
   - `TYPST=.tools/typst/typst-x86_64-unknown-linux-musl/typst make paper`
2. Stage consistency checks used by the paper/README
   - `python3 scripts/current_stage_report.py --check`
   - `python3 scripts/eval_matrix.py --check`
3. Citation hygiene checks
   - grep citation keys from section files and ensure they exist in `paper/refs.bib`
   - manually inspect any newly added refs for scope match
4. README alignment
   - ensure README reflects the final paper claims and current project stage after paper verification

## Critical content checks
- No paper section still reports the obsolete `pp512≈245` / `tg128≈3.1-3.4` result as the current Vulkan outcome.
- No appendix or discussion section still centers removed legacy-llama-substrate rows as active results.
- Dependency analysis explicitly contrasts Linux's broader DRM/Mesa tower with VOGUE's narrower library chain and shared protocol seam.
- Claim boundaries remain explicit: PASS rows require same-run evidence; future work stays future work.
