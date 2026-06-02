# Test spec: rename canonical tests/results and remove `latest`

## Validation
1. Targeted native test/build checks still pass:
   - `make -C tests native`
   - `make naming-check`
2. Repo scripts that read/write renamed artifacts still pass:
   - `python3 scripts/benchmark_summary.py --check`
   - `python3 scripts/current_stage_report.py --check`
3. README/project consistency still passes:
   - `make app-port-check`
   - `make lib-readme-check`
   - `make paper-check`
   - `make current-stage-check`
4. Search guard:
   - `rg -n '/latest[./]|_latest\b|latest-plan'` should find no canonical artifact paths in tracked project content except intentionally preserved external log text or historical data payload text.

## Risks
- Embedded path strings inside generated JSON/MD need rewrite, not only code paths.
- Some checks may depend on exact filenames and must be updated in lockstep.
