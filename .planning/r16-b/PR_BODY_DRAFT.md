## hardening(r16-b): science-context authority lifetime + decision-plane audit (dual track)

Baseline `origin/master` = `46791bc65c6f552ebbcf723bb91892736d56f319` (2026-10-17, fetched + pruned). Two isolated track worktrees (`hardening/r16-b-t03-…`, `hardening/r16-b-t04-…`) cherry-picked into this integration branch with source SHAs preserved; one PR per the R16-B plan, no merge, no CI wait.

### Track 03 — Scientific State + Science Context (fix)

**P1 (PR #1505 review, reproduced on master):** `ScienceContextBroker::capabilityFacts()` returned a raw pointer into `std::optional` storage; the `scientificCapabilities` MCP tool kept it past the broker lock while `setCapabilityFacts` (production installer: `src/agent/data_platform_tools.cpp:1509`) move-assigned the stored lookup. Deterministic RED: a view taken under authority A answered `"capability_knowledge.second"` / 0 entries / `revision()==9` after authority B was installed (3 CHECK failures, `9 == 7` expansion). Concurrently the same window is a torn read / `bad_function_call`.

Fix: `mCapFacts` is `std::shared_ptr<const CapabilityFactsLookup>`; installs create a fresh object and swap the pointer under the lock; `capabilityFacts()` returns an owned snapshot; the tool holds it for the whole routing call. No observable change for non-concurrent callers; a broker move no longer dangles outstanding snapshots.

**Verified already fixed on master (no re-fix, evidence tables in `.planning/r16-b-t03/`):** #1465 (broker failureDigest cache key; 4× `std::equal` length gates; static-init ec-form probes), #1447 `capability_router` isInt gate, and the #1505-review recipeSearch P1 (RecipeRouter independently synchronized — regression added here).

### Track 04 — Suitability / Preflight / Planner / Repair (audit, zero speculative code)

Full read of the decision surfaces + pattern scans found **no live defect** on master: #1466 P1 (`boundedIntegral` fail-closed) and #1447 (`capability_mirror` parseText guard) confirmed fixed with named regressions; determinism / fail-closed / rule-termination / golden course-scenario oracles confirmed covered by existing suites (matrix with file:line in `.planning/r16-b-t04/BASELINE.md`). Per the anti-fabrication rules this track lands documentation + the budget-caps inventory (`PERFORMANCE.md`) + integration-branch verification runs, not invented changes.

### Tests (real runs, `ctest -j1`, offscreen, integration branch)

(see TEST_RESULTS.md — filled after runs)

### Independent review

Round 1 (fresh-context reviewer subagent): **PASS_WITH_P2** — core fix correct/minimal; findings were evidence-doc defects (ledger RED narrative mechanically impossible as transcribed; missing TEST_RESULTS artifact; prefix mismatch) + 5 P3s, **all fixed** in the review-fix commit. Round 2: (filled after run).

### Cross-PR surface

No `tests/CMakeLists.txt` edits (avoids #1515's diff); no files overlap with open PRs #1513–#1516. Out-of-ownership #1465 sub-item (`src/help/terminology_provider.cpp`) recorded, untouched.

(filled: test numbers, review round 2 verdict, unsolved P2s)
