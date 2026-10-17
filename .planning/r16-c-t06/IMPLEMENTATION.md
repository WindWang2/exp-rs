# R16-C/T06 — teacher batch assessment hardening: implementation log

Branch `hardening/r16-c-t06-teaching-admin` (4 commits on `origin/master` 46791bc65).
All four fixes were implemented test-first (RED on pristine src, then GREEN); the
RED/GREEN evidence below is cited from the commit messages, and every GREEN count
under "Verification" was re-run fresh by the finishing verifier on final HEAD
(see `TEST_MATRIX.md` for commands).

Scope: `src/teaching_admin/**` only; 4 new test suites registered append-only in
`tests/CMakeLists.txt` (lines 15941-15944) via the existing
`sicnu_add_teaching_admin_test` helper.

---

## WP1 — checkpoint stamp fingerprints content, not stat alone

- **Commit:** `ac3f345429263b816c91399538ae1fc4a4cd755e`
- **Files:** `src/teaching_admin/batch_assessment.cpp` (artifactStamp),
  `tests/test_teaching_admin_checkpoint_stamp_r16_c_t06.cpp` (new, 2 cases),
  `tests/CMakeLists.txt` (registration).

### Problem
`artifactStamp()` was `mtime:size` only. A content swap preserving both — `cp -p`
with a backdated mtime, or a same-length rewrite with the mtime restored — made
checkpoint resume adopt the stale durable row over the re-submitted artifact, so
the #1448 re-grade gate never fired and the class report published the old grade
for new bytes.

### Root cause
A stat-only proxy cannot distinguish a bit-identical-stat rewrite; it was
documented as a cheap proxy that "only has to catch re-submissions", which is
exactly what it failed to do.

### Fix
The stamp now appends a bounded sha256 of the artifact's first 64 KiB
(`mtime:size:headHash`), keeping the string opaque to the checkpoint schema.
Old `mtime:size` stamps simply stop matching → fail closed to re-grading. An
unreadable artifact yields an empty stamp → also fail closed to re-grading.

### RED evidence (as recorded in the commit message)
> RED (pristine src): test_teaching_admin_checkpoint_stamp_r16_c_t06::
> 'checkpoint stamp rejects a same-stat content swap' — resumed==3,
> regrades==0, s002 row kept the stale pass/90.0 (4 assertion failures).

### GREEN evidence
- Commit message: "GREEN x2: 47/47 then 54/54 across checkpoint_stamp +
  admin_core + batch_discovery_r10 (+ script_adapters_r8 in round 2)."
- Fresh verifier runs on final HEAD: family 64/64 passed, run 1 and run 2,
  including both cases of this suite
  (`checkpoint adoption survives an untouched restart (stamp stability)`,
  `checkpoint stamp rejects a same-stat content swap`).

### Autonomous decisions
- Head hash bounded to 64 KiB to keep the resume fast path cheap for classroom
  rasters (full-file hashing deliberately avoided).
- Stamp encoding kept opaque/evolvable per #1448; no checkpoint schema change.
- Unreadable artifact → empty stamp (fail closed) rather than a stat-only
  fallback.

---

## WP2 — grader transcript read budgeted by maxBytes

- **Commit:** `9e359dbcc040cad8e38f189b58d023e34c690e58`
- **Files:** `src/teaching_admin/grader_cli_adapter.cpp` (gradeViaCli),
  `tests/test_teaching_admin_grader_cli_budget_r16_c_t06.cpp` (new, 2 cases),
  `tests/CMakeLists.txt` (registration).

### Problem
`gradeViaCli()` capped captured stdout/stderr (#1466) and forwarded
`--max-bytes` to the CLI, but read the `--out` transcript back with an unbounded
`readAll()`: the transcript channel bypassed the byte budget entirely and an
oversized document was parsed and graded verbatim.

### Root cause
The budget was enforced on capture (stdout/stderr) but not on the transcript
file read, even though the transcript is the preferred grading channel.

### Fix
The transcript read goes through `readBounded()` keyed to the same budget
(`cfg.maxBytes > 0 ? cfg.maxBytes : GraderCliConfig{}.maxBytes`). An oversize
transcript is a typed unavailable row (`grader_transcript_oversize`), never a
parsed grade.

### RED evidence (as recorded in the commit message)
> RED (pristine src): test_teaching_admin_grader_cli_budget_r16_c_t06::
> 'gradeViaCli refuses an over-budget transcript instead of grading it' —
> the 16 KiB valid pass transcript against a 2 KiB budget graded as
> pass/88.5 (4 assertion failures, unavailableReason empty).

### GREEN evidence
- Commit message: "GREEN x2: 46/46 twice across grader_cli_budget + admin_core +
  script_adapters_r8."
- Fresh verifier runs on final HEAD: family 64/64 passed, run 1 and run 2,
  including both cases of this suite
  (`gradeViaCli refuses an over-budget transcript instead of grading it`,
  `gradeViaCli grades a within-budget transcript normally (control)`).

### Autonomous decisions
- Non-positive `cfg.maxBytes` falls back to `GraderCliConfig{}.maxBytes`
  (wrapper default) rather than treating 0 as "unbounded".
- The oversize refusal keeps the wrapper-default `started=false`; only the
  crashed-CLI path marks `started=true` (the refusal is the contract).
- Fake CLI is a POSIX `/bin/sh` script; the cases `SUCCEED()` on Windows where
  a `.cmd` twin cannot be verified.

---

## WP3 — rubric weights must be finite and non-negative

- **Commit:** `fce217cbf026a2b2757a8b56560381f250bd50c2`
- **Files:** `src/teaching_admin/rubric_builder.cpp` (validateLabRules,
  validateProcessRubric),
  `tests/test_teaching_admin_rubric_weights_r16_c_t06.cpp` (new, 7 cases),
  `tests/CMakeLists.txt` (registration).

### Problem
`validateLabRules()`/`validateProcessRubric()` only checked the aggregate sum
(lab: `sum > 0`; process: `|sum - 1.0| <= 1e-6`). A negative weight cancelling
against a bloated partner passed both (a deduction-weight model where one
assertion adds points by failing); a `+inf` overflow sum passed `sum > 0`; a
NaN sum made `|sum - 1.0| > eps` silently FALSE.

### Root cause
Aggregate-only arithmetic checks without per-item domain constraints and
without finite-domain guards on the sum comparison.

### Fix
Per-item weights must be finite and non-negative (`malformed_grading_rule`);
both aggregate checks refuse non-finite sums (`weight_mismatch`), with the lab
message distinguishing "finite total" from "positive total".

### RED evidence (as recorded in the commit message)
> RED (pristine src): 4 of 7 cases in
> test_teaching_admin_rubric_weights_r16_c_t06 failed (negative lab
> weight, inf lab sum, negative process weight, NaN process weight all
> validated ok).

### GREEN evidence
- Commit message: "GREEN x2: 44/44 twice across rubric_weights + admin_core
  (which re-covers the existing weight_mismatch cases)."
- Fresh verifier runs on final HEAD: family 64/64 passed, run 1 and run 2,
  including all 7 cases of this suite (negative lab weight, inf lab sum,
  negative process weight, NaN weight, inf process sum, no-over-rejection
  control, checkWeightSum NaN regression).

### Autonomous decisions
- Zero weights remain allowed (finite and non-negative — existing behavior
  preserved, guarded by the no-over-rejection control case).
- `checkWeightSum()` already failed closed on NaN; only a regression case was
  added for it, no source change.
- Distinct error message for non-finite lab sums vs non-positive ones.

---

## WP4 — batch row identity comes from discovery, not the callable

- **Commit:** `4be6a838e6b646e458e92e248d92edacb22dd374`
- **Files:** `src/teaching_admin/batch_assessment.cpp` (runBatchAssessment),
  `tests/test_teaching_admin_student_id_authority_r16_c_t06.cpp` (new, 1 case),
  `tests/CMakeLists.txt` (registration).

### Problem
`runBatchAssessment()` isolated throwing callables and backfilled EMPTY row
fields, but published a callable-supplied `studentId` verbatim: a callable bug
(an adapter mapping capsule ids wrong) attributed one student's score to
another id in the published JSON/CSV — the exact misattribution the
counting-truth contract forbids.

### Root cause
Identity was trusted from the untrusted boundary: only empty studentIds were
backfilled from discovery, so a non-empty wrong id survived to publication.

### Fix
The worker overwrites `row.studentId = items[i].studentId` — discovery owns the
(student, artifact) pair; the grade itself stays the callable's.

### RED evidence (as recorded in the commit message)
> RED (pristine src):
> test_teaching_admin_student_id_authority_r16_c_t06 — the imposter id
> s999-imposter appeared in report.rows and report.toJson() (3 assertion
> failures).

### GREEN evidence
- Commit message: "GREEN x2: 48/48 twice across student_id_authority +
  checkpoint_stamp + admin_core + batch_discovery_r10."
- Fresh verifier runs on final HEAD: family 64/64 passed, run 1 and run 2,
  including this suite's case
  (`batch rows are attributed to the discovered student, never to a
  callable-supplied id`).

### Autonomous decisions
- Overwrite rather than backfill-empty: a wrong non-empty id must not survive.
- Other row fields (labId, rubricVersion, ...) keep their backfill-if-empty
  semantics; only identity changed authority.
- The grade values (including the imposter's inflated 100.0) are kept verbatim —
  the test asserts this to pin the contract boundary.

---

## Defects found by the verifier

None. All four fixes were re-read line-by-line, rebuilt, and re-run green twice
on final HEAD; registration and whitespace (`git diff --check
origin/master..HEAD` clean) verified. No minimal fix-ups were needed.

## Round-1 review fixes (integration branch)

Independent round-1 review (full-diff adversarial audit by a reviewer that did
not implement the fixes) returned P0=0, P1=1, P2=4. All were addressed on the
integration branch, each with RED on the pre-fix HEAD unless honestly noted:

| Finding | Sev | Fix | Commit | RED |
|---|---|---|---|---|
| Head-only stamp adopts stale rows over tail-only same-stat swaps (>64 KiB artifacts) — empirically demonstrated by the reviewer's probe | P1 | stamp fingerprints head AND tail (second 64 KiB window seeked from the end; opaque encoding unchanged; old stamps fail closed to re-grading) | `6117694f9` | yes — 200 KiB artifact, tail byte flip + exact mtime restore: resumed=1/regrades=0, 4 assertion failures |
| Unconditional identity overwrite publishes EMPTY id when discovery yields none (`_submission.*` flat root file) | P2 | overwrite only when the discovered id is non-empty; callable's id is then the only attribution | `6dd7afd6b` | yes — :165 empty row |
| `toDouble(0.0)` silently coerces string weights to a legal 0.0 | P2 | JSON-number type check → typed malformed_grading_rule, both dialects | `636d9f9c9` | yes — both dialects (:176) |
| `readBounded` unreadable ignored at transcript call site | P2 | typed `grader_transcript_unreadable` refusal when partial bytes were obtained (growth window); missing-file path KEEPS the stdout fallback, pinned by a new control test | `15e79bf1e` | honest note: the growth window is a two-line race inside readBounded, not deterministically triggerable via the public API — shipped without its own red test; fallback control GREEN both sides |
| T05 test binaries land in `build/tests/` instead of build root | P2 | `RUNTIME_OUTPUT_DIRECTORY` set for both r16_c_t05 targets | `15e79bf1e` | n/a (build hygiene) |

GREEN after all fixes: r16_c suites **23/23**; combined family
(`r16_c_t05|r16_c_t06|test_teaching_admin_core$|test_teaching_batch_discovery_r10|test_teaching_admin_script_adapters_r8|^test_lab_runtime|test_teaching_lab_cockpit|test_teaching_parity_r4|test_teaching_foundation_e2e|test_lab_pack_boundaries_r4`)
**67/67 ×2** identical counts; verify/portability family 12/12 (with
`test_verify_adapters_grid` actually built this time — its earlier
`_NOT_BUILT` placeholder was target selection, not a registration gap).
