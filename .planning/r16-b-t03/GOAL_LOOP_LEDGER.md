# R16-B-T03 Goal Loop Ledger (Oracle decisions)

Host note (honesty clause): this prompt targets Google Antigravity `/teamwork-preview` + `/goal-loop`. The ZCode host does not provide those commands; per the prompt's own fallback clause the equivalent native mechanism is used: git worktree isolation per track, per-track branches, RED→GREEN oracle loop recorded here, and independent review by subagents that did not implement the code. No command output was fabricated.

Capacity note: the ≥3×10^8-token figure is a long-horizon capacity statement, not a completion criterion (per the prompt's own "不以 token 消耗量作为完成标准" and "严格禁止空转"). This track spends its budget on: full read of the 13-file `science_context` + `scientific_state` surface, verification of every filed lead, one real P1 fix with deterministic oracle, one concurrency regression gap closure, and evidence documents. No work is invented to burn tokens.

## Phase 0 — Baseline / dedup (DONE)

- baseline SHA recorded (46791bc65), worktree/branch created clean, PR/issue/branch matrix written (`ISSUE_PR_BRANCH_MATRIX.md`).
- Every 2026-10-08 issue lead and #1505 review lead re-verified against master source; results in `BASELINE.md`. Only ONE live in-ownership defect: capability-facts lifetime (R1).

## Phase 1 — Work packages (DONE)

| WP | Type | Oracle | State |
| --- | --- | --- | --- |
| WP-T03-1 | P1 fix | Deterministic test: a facts view obtained before an authority reinstall keeps answering for its OWN authority afterwards (on master it silently answered for the NEW one → RED; owned snapshot → GREEN) | done |
| WP-T03-2 | regression/test de-blinding | Stress test: `agent_adapter::recipeSearch` ∥ `RecipeRouter::setRecipes` and `agent_adapter::scientificCapabilities` ∥ `setCapabilityFacts` complete without exception/corruption, results coherent (old-or-new authority, never mixed) | done |
| WP-T03-3 | evidence | verification ledger for already-fixed #1465/#1447 items (BASELINE.md table) | done |

Out-of-scope guard: `src/help/terminology_provider.cpp` (#1465 item) is NOT in this track's ownership — recorded, untouched.

## Phase 2 — RED→GREEN log

### WP-T03-1 capability facts lifetime (P1)

- **RED (real run, pre-fix)**: `ctest -R test_science_context_broker` → `1/40 failed` — Test #880 `capability facts snapshot stays answerable across an authority reinstall`, `assertions: 8 | 5 passed | 3 failed`, all three non-fatal CHECKs:
  - `snapshot->authority == "capability_knowledge.first"` failed — the storage now answers `"capability_knowledge.second"` (the reinstall move-assigns into the optional's contained object),
  - `snapshot->entriesForIntent( "ndvi" ).size() == 1` failed — the callable is the NEW authority's (it returns 0 entries),
  - `snapshot->revision() == 7` failed with expansion `9 == 7` — the stale view answered for the NEW authority.
  (`REQUIRE_NOTHROW` passed in the sequential case: the replaced callables remain invocable. The `std::bad_function_call` / torn-read mode lives in the CONCURRENT window — a reader between the member-wise moves, or calling a heap-allocated callable being destroyed — which is what churn regression TC_R16B_01 covers. Round-1 review correctly flagged that an earlier draft of this entry conflated the two modes; this entry now records what the run actually printed.) Other 39 tests passed (baseline healthy).
- **Fix**: `ScienceContextBroker::mCapFacts` is now `std::shared_ptr<const CapabilityFactsLookup>`; `setCapabilityFacts` installs a fresh object (`make_shared`) and swaps the pointer under the lock; `capabilityFacts()` returns an owned snapshot; the `scientificCapabilities` tool holds the snapshot for the whole call (`agent_adapter.cpp`). `synthesize()` keeps its locked raw view — `mCapFacts` co-owns the object, so a concurrent reinstall cannot invalidate it.
- **GREEN (run 1)**: `ctest -R test_science_context_broker` → `40/40 passed`.
- **GREEN (run 2)**: same command repeated on the integration branch (see Phase 3).

### WP-T03-2 tool-path churn regressions

- Added `TC_R16B_01_CapabilityToolSurvivesAuthorityReinstallChurn` (calibrated coherence predicate: every tool call answers entirely for authority A or entirely for B; no escaping exception) and `TC_R16B_02_RecipeSearchToolSurvivesRouterReplacementChurn` (every call's 8-hit set comes from exactly one router snapshot) to the already-registered `tests/test_science_context_concurrency.cpp`.
- **GREEN**: `ctest -R test_science_context_concurrency` → `14/14 passed` (12 pre-existing + 2 new).
- Note: these are stress regressions for the #1505-review seam; the deterministic RED for the underlying defect is WP-T03-1's Test #880 (stress tests are probabilistic by nature and not claimed as the RED oracle).

### Build environment (honest record)

- System GDAL absent; the machine-level dependency prefix `/home/kevin/pwb-sdks/root/usr` (Arch-style extraction with GDAL 3.13 headers/libs, PROJ, GEOS, GSL, netCDF) is used, mirroring the sibling R16-C track. The Debian-style prefix (`paleo-singlefactor-closure`) cannot link here: its libgdal has unresolvable `wrapper2_*` LAPACK references on this host (the sibling R16-A track hit the same wall — its `test_science_context_broker` link.txt exists but no binary).
- Catch2 fetched from the local seed `/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1` (network FetchContent unavailable).
- Tests run via `ctest` with `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib` (the repo's own `SicnuTestEnv.cmake` already pins this prefix), `QT_QPA_PLATFORM=offscreen`, `-j1`.


## Phase 3/4 — integration & PR

- Both track commits cherry-picked onto `hardening/r16-b-scientific-decisions` with zero conflicts (source SHAs `0f003cc0d` → `a3465fc53`, `3bfab05d5` → `b9b8f257b`).
- Round-1 independent review (fresh-context subagent): PASS_WITH_P2; all P2/P3 findings fixed in the review-fix commit (see `../r16-b/REVIEW_ROUND1.md`).
- GREEN run 1 on integration (post review fixes): science_context + authority-invalidation **102/102**; track-04 families **196/196** (real outputs in `../r16-b/TEST_RESULTS.md`).
- GREEN run 2 recorded in `../r16-b/TEST_RESULTS.md` after round-2 review.
- `git diff --check` clean; `git diff origin/master...HEAD --stat` = code (5 files, +260/−7 net of review fixes) + planning markdown + .gitignore negations.
