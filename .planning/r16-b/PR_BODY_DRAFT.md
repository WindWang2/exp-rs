## hardening(r16-b): science-context authority lifetime + decision-plane audit (dual track)

Baseline `origin/master` = `46791bc65c6f552ebbcf723bb91892736d56f319` (2026-10-17, fetched + pruned). Two isolated track worktrees (`hardening/r16-b-t03-…`, `hardening/r16-b-t04-…`) cherry-picked into this integration branch with source SHAs preserved (`0f003cc0d`, `3bfab05d5`); one PR per the R16-B plan. Not merging; not waiting on remote CI.

### Track 03 — Scientific State + Science Context (fix)

**P1 (PR #1505 review, reproduced on master):** `ScienceContextBroker::capabilityFacts()` returned a raw pointer into `std::optional` storage; the `scientificCapabilities` MCP tool kept it past the broker lock while `setCapabilityFacts` (production installer: `src/agent/data_platform_tools.cpp:1509`) move-assigned the stored lookup. Deterministic RED: a view taken under authority A answered `"capability_knowledge.second"` / 0 entries / `revision()==9` after authority B was installed (3 CHECK failures, expansion `9 == 7`). Concurrently the same window is a torn read / `std::bad_function_call` on a mid-move callable.

Fix: `mCapFacts` is `std::shared_ptr<const CapabilityFactsLookup>`; installs create a fresh object and swap the pointer under the lock (strong guarantee on `bad_alloc`); `capabilityFacts()` returns an owned snapshot; the tool holds it for the whole routing call. No observable change for non-concurrent callers; a moved-from broker degrades to the builtin fallback exactly as before, and outstanding snapshots now survive a broker move.

**Verified already fixed on master (no re-fix, evidence tables in `.planning/r16-b-t03/`):** #1465 (broker `failureDigest` cache key; 4× `std::equal` length gates; static-init `error_code` probes), #1447 `capability_router` `isInt` gate, and the #1505-review recipeSearch P1 (`RecipeRouter` independently synchronized — churn regression added here).

### Track 04 — Suitability / Preflight / Planner / Repair (audit; zero speculative code)

Full read of the decision surfaces + pattern scans found **no live defect** on master: #1466 P1 (`boundedIntegral` fail-closed) and #1447 (`capability_mirror` `parseText` guard) confirmed fixed with named regressions; determinism / fail-closed / rule-termination / golden course-scenario oracles confirmed covered by existing suites (matrix with file:line in `.planning/r16-b-t04/BASELINE.md`). Per the anti-fabrication rules this track lands documentation, the budget-caps inventory (`PERFORMANCE.md`), and integration-branch verification runs — not invented changes.

### Tests (real runs; `ctest -j1`, offscreen, `dev-default` Debug, `-j2` builds under a cross-prompt `flock` lease)

- **RED** (pre-fix, master sources + new test): `test_science_context_broker` 39/40 — full transcript in `.planning/r16-b/TEST_RESULTS.md`.
- **GREEN ×2** (integration branch, after review rounds): `test_science_context*` + `test_preflight_authority_invalidation` → **102/102**, twice; track-04 families (full preflight family, planner core/constraints/golden/proposal, suitability core/adversarial, repair completion/schema) → **196/196**, twice.
- ASan/TSan instrumented runs not executed (GDAL closure under sanitizers vs the host's local dependency prefix; lease shared with sibling R16 tracks) — recorded as an environment limitation in TEST_RESULTS.md, not claimed as PASS.

### Independent review

- **Round 1** (fresh-context reviewer subagent): **PASS_WITH_P2** — core fix correct/minimal; findings were evidence-document defects (RED narrative mechanically impossible as first transcribed; missing TEST_RESULTS artifact; prefix mismatch) + 5 P3s — **all fixed** in `78a84fa1f`.
- **Round 2** (second fresh-context reviewer, final gate): **PASS** — all round-1 fixes mechanically re-verified (incl. recounting test populations from the prebuilt binaries); remaining items were two P3 doc tallies and P4 observations, **all fixed** in the round-2 commit. Full reports: `.planning/r16-b/REVIEW_ROUND{1,2}.md`.

### Cross-PR surface & issue hygiene

- No `tests/CMakeLists.txt` edits (clears #1515's diff); no source-file overlap with open PRs #1513–#1516; `.gitignore` change is additive negations following the committed `.planning/r14-*` pattern.
- Out-of-ownership #1465 sub-item (`src/help/terminology_provider.cpp`) recorded in the matrix, untouched. No issues auto-closed; #1465/#1466/#1447 items are documented as already-fixed on master with evidence, not re-fixed.
- Worktrees/branches: `hardening/r16-b-t03-science-context-lifetime`, `hardening/r16-b-t04-decision-convergence`, `hardening/r16-b-scientific-decisions` (integration). No foreign branch, worktree, or build dir touched.

### Known unsolved / limitations

- Sanitizer-lane verification (above).
- P3-5 observation (round 1, pre-existing, unchanged): the snapshot pins the lookup seam object, not the authority data behind the stateless production closures; a knowledge-pack reload bypassing `setCapabilityFacts` can still tear revision-vs-entries within one synthesize (self-healing on the next call) — documented in `broker.h`, left for the authority owner.
- F2-6 (pre-existing dead includes) recorded, out of scope.
