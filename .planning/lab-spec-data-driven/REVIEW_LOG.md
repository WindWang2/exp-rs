# REVIEW_LOG — LabSpec Data-Driven Labs

Reviewers: reviewer-1 = adversarial code review (read-only subagent, full diff
`origin/master...HEAD` @ 9b7fcb45e2); reviewer-2 = independent static
verification of the completion gates (read-only subagent). Dispositions
applied in commit `1f63ee3386` (P7) unless noted.

### R1-1 [P0] Loader rejects every shipped LabSpec (completeBaseName keeps `.lab`)
- Source: reviewer-1
- Evidence: lab_spec_loader.cpp:102-104 — stem of `lab01_....lab.json` never equals the id
- Disposition: fixed — `QFileInfo::baseName()`; comment corrected.

### R1-2 [P1] lab11 `rs:obia_classify` runtime-fatal without training source
- Source: reviewer-1
- Evidence: rs_obia_classify_operator.cpp:27-31 throws unless exactly one of training/segmentClasses
- Disposition: fixed — step binds `training: data/samples/training_samples.shp` (same shape as lab03).

### R1-3 [P1] Duplicate-id guard dead code; its test unsatisfiable
- Source: reviewer-1
- Evidence: id==stem enforcement makes cross-file id collisions structurally impossible
- Disposition: fixed — dead branch removed from loader; test rewritten to pin the stem-mismatch rejection.

### R1-4 [P1] Run-button state machine vs async job (silent busy no-op, stray re-enable)
- Source: reviewer-1
- Evidence: showStep re-enabled Run mid-flight; busy-gate returned silently
- Disposition: fixed — showStep respects `isRunning()`; busy-gate reports a message; completion restores
  the button only when still on the same workflow+step.

### R1-5 [P2] submitJob() rejection strands Run at 运行中…
- Source: reviewer-1
- Disposition: fixed — taskId < 0 restores state and reports.

### R1-6 [P2] Schema/loader drift: params optional in schema, mandatory in loader; spec_version 1.0 edge
- Source: reviewer-1
- Disposition: fixed — missing params ⇒ empty object; integral floats accepted for spec_version
  (draft-07 integer); LABSPEC.md wording aligned.

### R1-7 [P2] HTML-escaping inconsistent for JSON-authored strings
- Source: reviewer-1
- Disposition: fixed — workflow titles/descriptions/step text escaped at every setHtml injection point.

### R1-8 [P2] Output-dir mkpath only scanned top-level params
- Source: reviewer-1
- Disposition: fixed — recursive walk mirrors resolveLabParamPaths depth.

### R1-9 [P2] LabSpecError.line never populated
- Source: reviewer-1
- Disposition: fixed — parse errors extract "Line N" into the structured field.

### R1-10 [P2] Generator markdown table cells unescaped
- Source: reviewer-1
- Disposition: fixed — `md_cell()` escapes pipes/newlines; string params rendered in backticks.

### R1-11 [P2] List-title pattern not tr()'d
- Source: reviewer-1
- Disposition: fixed — `QObject::tr("%1 · %2")` (free-function context).

### R1-12 [P2] Unanchored `output/` gitignore pattern
- Source: reviewer-1
- Disposition: fixed — anchored to `/output/`.

### Reviewer-2 static gate verification (G1–G10)
All 10 gates PASS @ 9b7fcb45e2 (before P7): labs tracked (11 files, samples untracked),
jsonschema 0 errors, id↔filestem agreement, 11/11/11 inventories, generator `--check` exit 0
with clean worktree, diff confined, zero factories, tr() compliance, planning files present,
5 coherent commits off `27b9aa0a63` with master untouched. P7 touches only files inside the
same allowed set; the gate-relevant outputs (docs) were regenerated in the same commit.

## Round 1 verdict: P0=1, P1=3 → all fixed; P2: 8/8 actionable items fixed.

Round 2 (re-review): performed as a focused self-review of commit `1f63ee3386` against the
12 findings — each fix re-derived from the cited operator/loader/adapter sources; runtime
confirmation via the local test run (see EVIDENCE.md).
