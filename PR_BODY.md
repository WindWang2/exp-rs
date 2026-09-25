# fix(teaching_admin,lab_pack): one pack authority, batch resilience, transcript parity

## Goal

R3 Track 09 (teaching-admin / authority-convergence / batch resilience). Scope: further functional completion, real wiring, correctness, stability and test convergence of EXISTING modules only — no new product direction, no second truth source.

Live master at branch time: `9b5ecafa1dec61b9ea384c932937236896156f50` (rebased on top of `c26389d06286c0e2600f2cd9e80007ab17b65d2c` work start; re-checked before PR).

## PR / issue dedup

At execution: 0 open issues; open PRs #1312–#1327 scanned — none touch `src/lab_pack`, `src/teaching_admin`, `src/agent/lab_data_pack*`, teaching docks or the lab pack/batch tests. History: #1287 delivered the real teaching-admin adapters; this PR continues its declared tail items (pack parser duplication, CLI/in-process parity, batch cancel/retry/durability).

## Root causes & oracles

### 1. Two pack parsers (agent authority vs admin validator) had already drifted

`teaching_admin` re-implemented sicnu.lab-pack/1 validation with its own rules and had drifted from the deployment authority (`sicnu::agent::LabDataPack`):

- admin accepted **uppercase** sha256 (authority pins 64 lowercase hex);
- admin accepted **empty `inputs`** arrays and then marked such packs `offlineAvailable=true` (allPresent over zero rows — fail-open);
- admin **defaulted** missing `provenance` to `generated-samples` (authority requires the field);
- admin did not require `license` / `pack_version`.

Fix: new Qt-free leaf `sicnu_lab_pack` (src/lab_pack) is the ONE parser/validator; `sicnu::agent::LabDataPack` becomes a pure Qt façade (public API unchanged for `src/cli/lab_self_check`), and admin `validatePackDocument`/`inventoryPacks` delegate via `PackVerifier::loadFromBytes`, keeping only the repo-root containment guard local. An authority-refused pack can never again be inventoried as available.

Oracle: `test_lab_data_pack` rewritten Qt-free with **hardcoded independent SHA-256 constants** (computed outside the code under test); `test_teaching_admin_core` gains `[authority_parity]` — **RED against the old validator** (verified by temporarily reverting the implementation), GREEN after delegation.

### 2. Batch orchestrator: documented concurrency that did not exist, silent truncation, no durability

- `maxConcurrency` was documented but the loop was serial; a 1000-student class graded at worker-speed × 1.
- `maxSubmissions` silently `resize()`d the item vector (undiscoverable data loss).
- Cancel semantics were correct but the only producer (UI) could never set the flag; no partial durability, so an interrupted 30-minute run restarted from zero.

Fix (all in `runBatchAssessment`, public call contract preserved):
bounded worker pool (clamped 1..16, atomic index pull), slot assembly in discovery order (byte-identical reports under any completion order), cancellation checked before item start (never-started ⇒ typed `cancelled`, never disguised failures), typed `report.truncated` (total stays DISCOVERED count), durable checkpoint (`QSaveFile` temp+rename after every completed row; config-digest guard; resume adopts matching (student, artifact) rows; cancelled items never checkpointed), `BatchProgressFn` thread-safe progress probe. `regradeTraceability` digest now excludes run-metadata (`resumed`/`truncated`/`cancelled_early`) so a resumed restart and a fresh run share the digest. En-route fix: `std::vector<bool>` slot flags raced across workers (bitset words share bytes → lost rows).

Oracle: `test_teaching_admin_core` — bounded-concurrency invariants (1 < maxInflight ≤ 4), typed cap (total==5, rows==2, truncated==3), cancel-resume identity (resumed==2, only 4 re-grades, digest equal to an uninterrupted run), foreign-checkpoint refusal, and a **1000 mixed-outcome scale oracle** (250 pass / 250 corrupted / 500 isolated errors, deterministic digest across runs, ordered rows, checkpoint holds 1000 rows).

### 3. CLI / in-process transcript mapping was private and unverified

`gradeViaCli`'s (exit code, transcript) mapper was an anonymous-namespace function with no parity evidence, so the teacher console and the process-isolated CLI could silently disagree.

Fix: exported as pure `gradeFromTranscript` (the ONE mapper for both paths); `test_lab_grading` gains a parity oracle over the REAL engine (`OutputVerifier::gradeArtifact`): for pass / fail / unverifiable / usage artifact classes the in-process result and the mapped transcript agree on verdict, status, score, digest and top deduction; exit-0-with-fail-transcript is refused (`grader_exit_verdict_mismatch`).

### 4. Teacher-notes leak oracle for course projections

`projectCourseHomePreview` drops `teacher_notes`, but nothing proved it. New `assertNoTeacherNotesLeak` (key + verbatim-string oracle over the authority vocabulary `objectives_zh` / `common_mistakes_zh{mistake_zh,why_zh,check_zh}` / `grading_hook_zh`) with mutation tests (3 mutants killed) and a repo-truth check against the committed curriculum.

### 5. Dock batch ran on the UI thread with an unreachable cancel flag

Worker thread (joined in destructor), Cancel button (typed cooperation — completed rows published as durable partial results), push-based progress (queued UI invocation; no checkpoint polling), checkpoint beside the published outputs so re-running resumes. Smoke: async round trip waits on the queued UI truth (frozen UI would hang the smoke), deterministic cancel case (20 × 1 s stub grades, click at done==8 → completed kept, ≥10 cancelled with no score, atomic JSON+CSV publication).

## Authority

- Pack contract: `sicnu_lab_pack` leaf is the single parser; agent façade and admin delegation add no semantics.
- Grading: unchanged authority (`OutputVerifier::gradeArtifact`); the mapper is a projection of the CLI contract, no scoring logic added or copied.
- Bundle digest: untouched (`verify_bundle_manifest.py` stays canonical).

## Changed files

- new: `src/lab_pack/{lab_pack.h,lab_pack.cpp,CMakeLists.txt}` (leaf, layer-guarded: jsoncpp + sicnu_grader only)
- `src/agent/lab_data_pack.{h,cpp}` → façade; `src/agent/CMakeLists.txt` (+sicnu_lab_pack)
- `src/teaching_admin/data_pack_manager.cpp` (delegation), `batch_assessment.{h,cpp}` (resilience), `grader_cli_adapter.{h,cpp}` (mapper export), `curriculum_editor.{h,cpp}` (leak oracle)
- `src/app/teaching_admin/teaching_admin_dock.{h,cpp}` (worker/cancel/progress)
- `CMakeLists.txt` (+add_subdirectory(src/lab_pack)); `tests/CMakeLists.txt` (leaf-only pack test; teaching_admin link for parity test)
- tests: `test_lab_data_pack.cpp` (rewrite), `test_teaching_admin_core.cpp` (+4 cases), `test_lab_grading.cpp` (+parity), `test_teaching_admin_dock_smoke.cpp` (+cancel, async round trip)

## Targeted build & tests

Narrow target map (scripts/dev/narrow_targets.py): `Sicnu::teaching_admin`, `sicnu_agent`, `test_build_wiring_drift`, `test_lab_data_pack`, `test_lab_grading`, `test_teaching_admin_core`, `test_teaching_admin_dock_smoke`.

- `test_lab_data_pack`: 13 cases / 181 assertions — all pass
- `test_teaching_admin_core`: 34 cases / ~3500 assertions — all pass (incl. 1000-scale, checkpoint, leak mutations)
- `test_teaching_admin_dock_smoke`: 3 cases / 102 assertions — all pass (offscreen)
- `test_lab_grading [parity]`: run locally, all pass
- Gates re-run twice at the end; `test_build_wiring_drift` run (CMake changed).

## NOT a full build

Per repo policy this PR does NOT run `cmake --build <dir>` without `--target`. The heaviest target actually built is `test_lab_grading` (a named test target; the pack contract previously forced this closure on every pack test — after this PR the pack tests link only the leaf). Consumers of `sicnu_agent`/`sicnu_teaching_admin` outside these targets are relinked by CI; public headers changed are additive or façade-identical.

## Performance / resource

- Batch: N submissions at bounded W workers — wall time ~⌈N/W⌉ × per-grade latency instead of N × latency (1000-item scale test runs in-process in seconds; lockstep progress evidence in the dock smoke).
- Memory: unchanged streaming/bounded-hash policy; checkpoint rewrites O(rows) JSON after every row (bounded by class size, same as the row table itself); atomic per write, never a torn document.
- Pack test target: links the 600-line leaf instead of the full agent closure (large compile-time cut for the pack lane).

## Review findings & fixes

(Independent adversarial review pass; P0/P1/P2 findings and their resolutions — filled at PR time.)

## Known limits

- Checkpoint adoption keys on (student_id, artifact_path) + config digest; renaming files between restarts re-grades them (conservative).
- The parity oracle drives the in-process engine against the mapper, not a live `sicnu_geo_rs_cli` subprocess (the opt-in real-CLI lane already exists in `test_teaching_admin_core` via `SICNU_GEO_RS_CLI`).
- Mid-run checkpoint files can transiently read empty (QSaveFile open→commit window) — the UI reads progress via the push callback, never via the file; final checkpoint content is atomic.

## Rollback

Revert the PR; no data/schema migrations, no persisted-format changes outside the new (additive) checkpoint document `sicnu.teaching.batch_checkpoint/1`.
