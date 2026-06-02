# Autopilot context snapshot
- activation_prompt: Refresh virtio-gpu/paper to SOSP-level, remove stale parts, add dependence analysis/differences, improve prose/citations, then update README to match.
- original_task_status: activation-prompt
- desired_outcome: Paper matches current project stage, has stronger structure/style/citations, includes architecture/dependency analysis, and README reflects the final paper/project state.
- known_facts: Current paper exists under paper/; architecture/dependency context is in docs/ARCHITECTURE.md; user explicitly requires online/offical-resource-grounded writing and citation audit; README update must happen after paper work.
- constraints: Must use current project evidence; remove stale claims; citations must exist and fit claims; final README should reflect final state.
- unknowns_open_questions: Which sections are most stale; whether references.bib is current; which official style sources best fit SOSP/ACM expectations.
- likely_touchpoints: paper/main.typ, paper/sections/*.typ, paper/references.bib or bibliography files, docs/ARCHITECTURE.md, README.md.
- scope_note: Seed is the current Autopilot activation prompt for docs/paper work.
