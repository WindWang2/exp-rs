# DEDUP — flash-workflow-engine-12

## Overlap verdicts (checked 2026-09-20, live state)

1. **No open PR covers this track.** #1116–#1124 are data/model/spectral/lab/workbench/
   verification/fuzz/devtool tracks; none implements workflow IR versioning,
   composition, resume state machine, artifact contract, or provenance graph.

2. **Shared-file overlap:** PRs #1117, #1118, #1119, #1120 each touch exactly one
   src/workflow file — `pipeline_run_coordinator.cpp` — all applying the SAME
   minimal rename of `resumedDef` (a pre-existing GCC "conflicting declaration"
   break on master). Our plan: fix it canonically once (single rename); parallel
   PRs that merge first win the hunk, we rebase.

3. **`agent/flash-workflow-integrity`** (remote branch, diverged ~78 commits):
   mostly superseded — bounded checkpoint reads, meta.ui guard, artifact
   confinement, resume/cancel fix, port-wiring signatures all landed via other
   PRs. Still-unmerged deltas (marshalBlocking, resumed-artifact verification,
   path_containment.h, workflow_limits.h, ir2 executor hardening, 4 test files)
   are absorbed as *requirements* into this track's design — not cherry-picked.

4. **Parallel-track boundaries:**
   - Workbench (#1121): may consume workflow APIs; we keep GUI read-only.
   - Scientific Verification (#1122): provenance/reproducibility overlap — it owns
     experiment-level provenance; our provenance graph is workflow-run-level
     (run/node/input/output). Distinct layers; coordinate on shared vocabulary only.
   - Fuzz (#1123): owns generic fuzz lanes; our recovery/fuzz is workflow-domain
     fixtures, not overlapping.

5. **Sibling agent worktrees** (flash-temporal-phenology-12, flash-taskcenter-runtime-12,
   flash-plugin-sdk-12) exist locally at adf8f9895 — scheduled runs died on CLI auth;
   no workflow-scope work expected. Re-check before PR.
