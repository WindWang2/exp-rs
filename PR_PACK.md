# feat(autonomy): RS14-12 teaching autonomy ladder — L0..L5 policy gates before the action

Generated: 2026-09-22

## Baseline

- base `origin/master` @ `4f6632e1f6bb41f90729800d0c7bf569ff34edb3` (Merge pull request #1145)
- head `HEAD` @ this branch (`agent/rs14-teaching-autonomy-ladder`)
- commits in range: 8 (one logical slice each)

## Scope

- sources: `src/agent/autonomy/{autonomy_level,autonomy_capability,autonomy_classification,autonomy_policy,autonomy_decision,autonomy_audit,autonomy_projection,autonomy_holder}.{h,cpp}` + `fake_action_provider.h` (new Qt-free static lib `Sicnu::autonomy`), `src/agent/harness/lab_copilot.{h,cpp}`, `src/agent/harness/plan_tools.cpp`, `src/agent/harness/autonomy_tools.{h,cpp}` (new), `src/agent/harness/harness_error.{h,cpp}`, `src/agent/harness/tool_manifest.cpp`, `src/agent/spatial_tools/spatial_tool.cpp`
- build: `CMakeLists.txt` (+1 add_subdirectory), `src/agent/CMakeLists.txt` (+link +2 sources), `src/agent/autonomy/CMakeLists.txt` (new)
- tests: `tests/test_autonomy_{policy,classification,decision,precedence,audit,projection,holder,gate}.cpp` (new), `tests/CMakeLists.txt` (+`sicnu_add_autonomy_test` helper, +8 registrations)
- data/docs: `data/labs/labspec.schema.json` (optional per-lab `autonomy` block), `docs/agent/autonomy-ladder.md` (new), `docs/adr/0176-teaching-autonomy-ladder.md` (new), `PR_PACK.md`

## Non-goals

- No model chat UI redesign; no planner reimplementation; no sensitive user profiles.
- No durable (cross-restart) audit persistence — the audit log is in-process and bounded (1000, FIFO).
- Gates are wired at the two action seams that matter for teaching (the copilot entry point and the plan executor). Other mutating tools (e.g. `cartography:repair`) are not individually gated; the registry-decorator extension point is documented in `docs/agent/autonomy-ladder.md` and is a deliberate follow-up (a registry-wide gate touches every agent flow and needs its own matrix).

## Design

One closed ladder (`L0 no_assistance · L1 concept_hint · L2 error_localization · L3 next_step_recommendation · L4 plan_generation · L5 autonomous_execution`, plus `read_only_query` at L0) is the unit of every decision. Lab intents and tool risk classes classify onto the closed capability taxonomy (fail-closed: unknown input never maps to a more capable class). `sicnu.autonomy-policy/1` documents are strictly parsed and resolved by exactly one merge rule (`course < labspec < teacher < session`; per field the highest declaring source wins; `max_level` takes the tightest value). Modes bound the ladder (`exam`≤L2, `practice`≤L4, `instructor`≤L5, `agent` = explicit L5 opt-in). The engine decides allow/deny/downgrade with nine closed reason codes; assistive requests above the effective level downgrade to the highest unlocked capability, L0 denies (a "read-only" answer is still help), and execution is never silently substituted. Two gates: the assistance gate inside `labAsk()` and the execution gate inside `ExecutePlanTool::execute()` before preflight/compile/submit. Session policy layers are privileged (they can raise a level) and follow the teacher-credential gate, so a self-injected block cannot escalate a session. Structural rules outrank overrides: lab students never receive autonomous execution (ADR 0155 restated); non-lab L5 execution requires explicit agent mode and keeps scientific verification mandatory. The default course policy is the research default (L5/agent), so pre-autonomy research flows are behavior-compatible.

## Issue mapping

None. New capability layer; no existing issue closed or touched.

## Dynamic dedup (at PR time)

- `origin/master` unmoved since the baseline (`4f6632e1f6`).
- 14 open RS14 sibling PRs reviewed: no functional overlap. Nearest neighbours and the boundary: #1197 (LabSpec 2.0) also touches `data/labs/labspec.schema.json` — both changes are additive (this PR adds the optional per-lab `autonomy` block; theirs bumps `spec_version`); #1190 (curriculum pack) organizes lab content, #1196 (process grader) grades process — both consume, not duplicate, this policy layer through the documented DTOs. #1191/#1196 create `docs/integration.md`; this PR deliberately does NOT create that file (its wiring points live in `docs/agent/autonomy-ladder.md`) to avoid a create/create conflict.
- ADR numbers 0172–0175 are taken by sibling tracks (#1191 verifier, #1193 capability graph, #1194/#1196/#1199, #1201); this track's ADR is 0176.
- The open-issue exclusion list (SAR correctness, mission runtime, Workflow/D17, ImportCenter, jsoncpp, NoData, capability mirror, plugin lifecycle, TaskCenter, Catalog/Dataset/Experiment, geospatial mirror, atomic publish, WBF perf, perf baseline, test oracle, georeferencer UAF, CLI concurrency, P3 batch) has no overlap with this layer; two pre-existing test failures (`test_labspec` ×2, `test_harness9_contracts` ×1) were verified identical on the untouched baseline and are recorded as observed, not fixed.

## Tests

Commands (worktree `build-dev`, `-j1`, `SICNU_SOURCE_DIR` exported):

```
./test_autonomy_policy         All tests passed (118 assertions in 11 test cases)
./test_autonomy_classification All tests passed (83 assertions in 7 test cases)
./test_autonomy_decision       All tests passed (106 assertions in 10 test cases)
./test_autonomy_precedence     All tests passed (52 assertions in 9 test cases)
./test_autonomy_audit          All tests passed (2028 assertions in 5 test cases)
./test_autonomy_projection     All tests passed (57 assertions in 8 test cases)
./test_autonomy_holder         All tests passed (15 assertions in 3 test cases)
./test_autonomy_gate           All tests passed (143 assertions in 14 test cases)
```

The heavy gate suite drives the real seams (labAsk, SpatialToolRegistry execute) and includes the attempted-bypass matrix: a student routed at the executor gets `TEACHING_REFUSAL`; a forged teacher claim cannot escalate; the direct registry execute (the MCP/CLI path) refuses before the workflow engine; a self-injected session block without the host credential is ignored; the research default keeps pre-autonomy flows behavior-compatible (a structurally invalid plan still fails with `INVALID_PLAN`, never an autonomy code).

Regression (existing suites, same build): `test_harness_lab_evals` 561/561, `test_harness_lab_injection` 93/93, `test_harness_catalog` 199/199, `test_agent_tool_catalog` 5261/5261, `test_lab_grading` 585/585. `test_labspec` (2) and `test_harness9_contracts` (1) fail **identically on the untouched baseline** (lab12/13/14 ship without `steps`; `cartography/quality.cpp` emits `snap_align`/`balance_whitespace` keys outside the action table) — pre-existing, recorded as observed, not fixed here (out of scope).

## Resources

Build parallelism `-j2` for the one-time worktree configure/build, dropped to `-j1` after transient GCC-16 ICEs appeared under memory pressure (qgis_gui / sicnu_processing TUs — unrelated to this change; all succeeded on the `-j1` retry). Only the targets needed for this track were built: `sicnu_autonomy`, `sicnu_agent`, and the eight new plus seven regression test executables. No clean builds, no full-suite runs, no sanitizer lane.

## Review disposition

Round 1 (implementation review) findings, all fixed:
- **P0** `constantTimeEquals` typo introduced while moving the credential gate (`return diff` instead of `return diff == 0`) rejected every teacher credential — caught by the gate suite, fixed, and covered by the credentialed-session test case.
- **P1** `tool_manifest` classified `harness:execute_plan` as `read_only`, so the execution gate would have allowed the highest blast-radius tool at any level — fixed by adding the manifest row (`creates_artifact`); the suite pins the classification through the mirror floor.
- **P1** the plan gate read `domain` from the unauthenticated session block — a self-injected block could re-scope the request; now the whole session block (policy and context) is ignored without the host credential, and a forged instructor role degrades to student.
- **P2** session/labspec policy blocks without the `schema` field are silently ignored (strict parse) — documented; the LabSpec schema declares the field as required.
Round 2 (re-review after fixes): targeted regression green; no new P0/P1/P2 open.
