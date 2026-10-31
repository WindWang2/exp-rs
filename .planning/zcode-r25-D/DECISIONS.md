# R25-D DECISIONS

D1. **No re-fixing of swarm-review issues**: all 13+ triaged candidates verified FIXED_IN_MASTER_BUT_OPEN (see ISSUE_TRIAGE.md). Track value redirected to audit-driven deepening.
D2. **Availability rule granularity**: cartography.* gates on canvas layer PRESENCE (layerCount>0), not selection — compose drafts from canvas order; the dock provider gained the promised canvas fallback so the rule and the behavior agree (had they diverged, Loop 1 would have introduced a new fake-availability).
D3. **Preflight line source**: AlgorithmMetaStore sidecars (agent-shared, gpuDeclared rule #707) rather than operator executionEstimate() — capability knowledge with governance; estimate() stays an option for later merge.
D4. **Designer hand-off via signal**, not direct dialog from the dock: preserves the dock's no-window rule; the shell opens the SAME QgsLayoutDesignerDialog project.newLayout uses (one editor).
D5. **History unification**: ribbon+menu project the registry command; the QGIS LocalProfile modal deleted — it displayed a registry this product never writes.
D6. **Perf evidence is structural (counts) + recorded measurements**, never wall-clock pass gates (#1622 discipline).
D7. **Dependency cache reuse**: github.com unreachable (TLS eof) from this host; FetchContent sources (catch2 v3.7.1, pybind11 v2.13.6 — exact required tags) reused from the sibling r25-C cache via FETCHCONTENT_SOURCE_DIR_*; build dir strictly per-worktree.
D8. **adoptSpec made public** as the workflow/agent→dock handoff seam (also the test seam for composed-state gating).
