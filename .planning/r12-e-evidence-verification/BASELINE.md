# R12-E Baseline — verification / grading / faultlab / evidence chain

## Snapshot (commands actually executed 2026-10-14, host Linux)

- Repo: `WindWang2/exp-rs`, default branch `master`.
- `origin/master` (BASE_SHA): `28abef358217520dfbb802efdfdb11a2bf850465`
  (= "Merge pull request #1482", 2026-10-13 22:40:27 +0800). Local `master` at same SHA.
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-r12-e-verification`
- Branch: `hardening/r12-e-verification-evidence` (new, derived from BASE_SHA; merge-base verified == BASE_SHA).
- No pre-existing r12-e branch or worktree (`git show-ref` exit=1; worktree list has no r12-e entry).

## Recent master history (merge train)

#1475, #1476, #1477, #1478, #1479, #1480, #1481, #1482 all merged=true (verified via `gh pr list --state merged`).
Notable for us: `d5ab7bc0b0` fix(workflow) references #1448/#1450 (R11-C wave); #1476 touched faultlab review threads.

## Open PRs (active writers — must not collide)

| PR | Branch | Files overlapping R12-E? |
|---|---|---|
| #1483 | hardening/r12-a-science-numerics | src/analysis/**, src/core/temporal_cube.cpp, src/operators/rs/**, src/processing/algorithms/**, **tests/CMakeLists.txt**, tests/test_*science* | No source overlap with R12-E dirs. Shared: tests/CMakeLists.txt (locked, see OWNERSHIP.md). |
| #1484 | hardening/r12-c-agent-workflow-runtime | pi/**, src/runtime/chunk/scratch_registry.*, src/science_context/asset_state_provider.*, **tests/CMakeLists.txt**, tests/test_science_context_broker.cpp, tests/test_external_memory_10.cpp | No source overlap. `src/science_context/asset_state_provider.*` explicitly off-limits per directive. |

Review threads on #1483/#1484 via GraphQL `reviewThreads`: **zero threads** (both PRs have no reviews yet). `reviewDecision` empty.

## Open issues (state=open, limit 120): #1447–#1450, #1453–#1468 (20 items; not all contiguous)

R12-E-relevant leads (re-adjudicated against current master — see PR_ISSUE_REVIEW_MATRIX.md):

| Issue | Claim | Status on BASE_SHA |
|---|---|---|
| #1464 | grader recordedAtUtc lexicographic compare | ALREADY_FIXED — `parseSequencingStamp` in src/grader/grader_matcher.cpp:113 with calendar/leap/offset/fraction validation |
| #1464 | faultlab optional `cell_size` accepted-but-inert | ALREADY_FIXED — src/faultlab/fault_transforms.cpp:367-453 validates & wires `cell_size` → `extras["leakage_cell_size"]` (read by fault_observables.cpp:192) |
| #1465 | 4× 3-arg `std::equal` OOB in asset_state_json.cpp | ALREADY_FIXED — length gate before `std::equal` at lines 164/198/237/276 with #1465 comment |
| #1466 | suitability integer goal fail-open (`"min_samples":"200"`) | ALREADY_FIXED — `boundedIntegral` fails closed on non-double (src/suitability/suitability_goal.cpp:~54) |
| #1450 | suitability digests via QJsonDocument::toJson(Compact) | ALREADY_FIXED — both contentDigest() use `canonicalizeJsonRfc8785` (suitability_report.cpp:73, suitability_goal.cpp:341) |
| #1450 | faultlab canonical_json.cpp LC_NUMERIC drift | ALREADY_FIXED — `ClassicNumericLocale` per-thread pin (canonical_json.cpp:~77) |
| #1450 | four SHA-256 forks (verify/preflight/grader/faultlab) | **LIVE** — 4 md5-distinct files confirmed; no parity gate found |
| #1447 | faultlab fault_transforms role_a/role_b untyped asString | ALREADY_FIXED — type gates with typed refusal (fault_transforms.cpp:~93-129) |
| #1447 | preflight capability_mirror parseFromStream no guard | ALREADY_FIXED — try/catch + stackLimit=1000 (capability_mirror.cpp:23) |
| #1447 | faultlab fault_scenario.cpp:400 same shape | **LIVE (candidate)** — `loadFaultScenarioFile` has NO `stackLimit` and NO try/catch around `CharReader::parse`; grader_json.cpp:224 documents the reader can THROW on nesting bomb; fault_types.h declares never-throws boundary |
| #1450 | workflow_checkpoint bounded reader | OUT-OF-SCOPE (src/workflow → R12-C); R11-C commit d5ab7bc0b0 claims fix; not re-litigated |
| #1450 | radiometric vocabulary double-track | OUT-OF-SCOPE (src/core + src/operators/rs → R12-A domain; noted for coordination) |
| #1448 | stores fail-open | OUT-OF-SCOPE (workflow/stores → B/C) |
| #1466 | teaching_admin/teaching items | OUT-OF-SCOPE (R12-D domain) |

## Environment

- Compiler: /usr/bin/c++ (GCC), Qt6 at /usr/lib/cmake/Qt6, generator Unix Makefiles, Debug.
- Existing configured reference tree: `main/build-dev` (used as `--base-cache` for our configure).
- Resource discipline: `scripts/build.sh` (cap -j2, ctest -j1, QT_QPA_PLATFORM=offscreen); heavy builds under `flock "$HOME/.cache/exp-rs-heavy-build.lock"`.

## Test inventory (existing targets in tests/CMakeLists.txt, 15630 lines)

Relevant existing test files: test_grader_{engine,schema,adapters,render}, test_faultlab, test_faultlab_params_r8, test_fault_registry, test_fault_injection, test_fault_matrix, test_preflight{,_engine,_golden,_rules,_provider,_render,_report_schema,_check_tool,_authority_invalidation,_runtime_adapter}, test_suitability_{adversarial,core,labels,profiles,spatial,spectral,store_provider,teaching,temporal,uncertainty,agent_tools}, test_explain_{builder,validator,schema,projection,...}, test_scientific_state_{core,catalog,diff,fixtures,gdal,geo,nul_tokens_r8,provenance,resolver,review,teaching}, test_verify_adapters{,_grid}, test_verify_chain_locale_matrix, test_fault_injection, test_store_fault_injection.
Registration helpers: `sicnu_add_test(NAME ...)`, `sicnu_discover_tests(NAME ...)` (tests/CMakeLists.txt:41-146).
