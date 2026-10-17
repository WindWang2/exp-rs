# r16-c-t05 — student lab session & learning state (implementation log)

Track: undergrad remote-sensing lab journey reliability
(选择课程→加载样例→按步骤处理→查看错误→导出成果→提交→复盘).
Branch `hardening/r16-c-t05-teaching-session`, base origin/master 46791bc65.

Scope audited (read-every-line): `src/teaching/**` (lab_status,
course_home_view_model, lab_step_timeline, lab_readiness,
autonomy_effective_display, lab_feedback_projection, lab_session_state),
`src/lab/**` (session_state, session_store, checkpoint_verify, spec_runtime,
hint_policy, sha256), `src/lab_pack/lab_pack.cpp`, plus read-only consumers
`src/app/teaching/lab_cockpit_dock.cpp`, `guided_lab_workspace.cpp`,
`lab_validate_service.cpp` and the existing suites
(test_lab_runtime, test_teaching_lab_cockpit, test_teaching_parity_r4,
test_teaching_foundation_e2e, test_guided_lab_workspace_r8,
test_lab_pack_boundaries_r4).

Two fixes shipped (three defects — two landed in one commit because they
share the new suite file and one registration block):

## WP1 — forged row `counts_as_pass` survives feedback restore

- Commit: e904ab513 `fix(teaching): a forged row counts_as_pass never survives feedback restore`
- Files: `src/teaching/lab_feedback_projection.cpp`, `tests/test_teaching_restore_r16_c_t05.cpp` (new), `tests/CMakeLists.txt` (append-only block).
- Problem: `LabFeedbackProjection::fromJson` stripped `counts_as_pass` only
  when a row's status was `indeterminate`/empty. The persisted feedback
  summary (stored as `last_validation_summary` in the teaching session file)
  is untrusted input on the restart path; a hand-edited file could restore a
  row with status `fail` — or any non-pass word (`not_earned`, unknown enums)
  — carrying `countsAsPass=true`, contradicting the header invariant
  ("NEVER true" except pass; computed as `status == "pass"` on the live
  fromReports path) and re-serializing the contradiction into the next save
  cycle (`toJson` copies the flag verbatim).
- Root cause: restore sweep was weaker than the derivation it mirrors.
- Fix (1 line of logic): `if ( cr.status != "pass" ) cr.countsAsPass = false;`
- RED: new suite, pristine src — 2 of 4 cases failed
  (`REQUIRE_FALSE( restored.rows[0].countsAsPass )` → `!true`, both
  row-forgery cases). Distilled from `.planning/r16-c-t05/logs/red_teaching.log`.
- GREEN ×2: 4 cases / 32 assertions, both runs (binary + ctest).
- Decisions: kept the recorded `status` string untouched on restore (the
  class documents "keeps its recorded verdict"); only the derived flag is
  re-derived. No new refusal for unknown status words — that would change
  the restart contract beyond the invariant.

## WP2 — one unreadable lab directory truncated the WHOLE session listing

- Commit: 25ab7f6f0 `fix(lab): session store listing truncation + session identity coherence at load`
- Files: `src/lab/session_store.cpp`, `tests/test_lab_session_store_r16_c_t05.cpp` (new), `tests/CMakeLists.txt`.
- Problem: `LabSessionStore::listSessions` shared one `std::error_code`
  between the outer (lab-dir) and inner (file) directory iterations. When an
  inner `directory_iterator` construction failed (e.g. mode-000 directory),
  the still-set `ec` hit the outer loop's `if (ec) break` on the NEXT
  iteration and silently aborted the entire scan — every later directory's
  sessions vanished from the listing. The `Listing` header contract is
  "unreadable entries are skipped, not fatal"; the implementation violated
  it for directories (it already held for unreadable files).
- Fix: inner loop uses its own `innerEc`; on failure push a
  `lab.session.io` warning into `listing.warnings` naming the skipped
  directory and continue the scan.
- RED: new suite, pristine src — listing case failed at
  `REQUIRE_FALSE( listing.warnings.empty() )` (`!true`). readdir order is
  filesystem-controlled (verified on tmpfs: reverse creation order), so the
  missing-warning oracle is the deterministic reproduction; the truncation
  itself was reproduced directly in a standalone probe (denied dir iterated
  before readable dirs → outer loop breaks).
- GREEN ×2: 3 cases / 30 assertions.
- Decisions: warning not fatal (matches contract); test skips under root
  (`geteuid() == 0`, where mode bits don't deny reads).

## WP3 — session identity incoherence accepted at load

- Commit: 25ab7f6f0 (same commit as WP2 — shared suite + registration).
- Files: `src/lab/session_state.cpp`.
- Problem: `sessionFromJson` validated shape and the event-seq envelope but
  never checked that `session_id` agrees with the recorded `lab_id` /
  `student_id`. A hand-edited document could claim
  `session_id = "lab_a/student_x/1"` while recording `lab_id = "lab_b"`;
  it loaded fine, and downstream surfaces (store `Summary::labId`,
  `listSessions`) then reported a lab the session is not. The store derives
  its path only from `session_id`, so the mismatched identity also desyncs
  path vs. content truth.
- Fix: in the final `if (diags.empty())` gate (next to the R14-E seq
  envelope check) require `session_id == "<lab_id>/<student_id>/<digits>"`
  (non-empty, ≤18 digits — mirrors `sessionPathFromId`), else a typed
  `lab.session.identity` refusal.
- RED: pristine src — 3 forged-id sections accepted (`!true` on
  `REQUIRE_FALSE( refused.ok )`); positive round-trip section green.
- GREEN ×2: same suite.
- Decisions: digits-only suffix without leading zeros (nothing else is
  producible — `startSession` uses `std::to_string`); no cross-check against
  `last_seq` (the suffix is the creation sequence, not the ledger head).

## Audited, verified, no defect found (hardening-by-verification)

- **WP4 pins (in the new suites, green on pristine sources too)**:
  unicode store root round-trip (`/tmp/r16_c_t05_unicode_实验室根目录…`,
  byte-stable save/load/list); read-only lab directory → `save` fails typed
  `lab.session.io`, previous generation intact, no half products;
  teaching `LabSessionState` save/load under a Chinese-named path.
- `checkpoint_verify` / `completeSession` / `deriveStageStatus` latest-wins
  gate semantics: consistent across record/complete/derive paths (existing
  suite green ×2).
- `hint_policy` budget + escalation order, `spec_runtime` typed rejections,
  `sha256` (FIPS vectors pinned in suite): green ×2, no changes needed.
- `lab_pack` canonical hashing / root confinement: 29-case boundary suite
  green ×2, no changes needed.
- Dedup honored: `lab_session_state` sidecar write + strict schema (R6),
  `session_state` seq envelope (R14-E) — covered by existing suites, not
  redone.

## Known limitations recorded (not fixed — design notes)

- `LabSessionStore::create` sequence allocation is scan-then-use (TOCTOU):
  two processes creating concurrently before either saves can both draw the
  same next sequence; the later atomic save replaces the earlier file
  (last-writer-wins, no torn files thanks to the sidecar authority).
  Fixing it would require claiming the file at create() time (semantics
  change to create/persist flow) — out of "minimal surgical" scope for this
  round; single-process flows (the CLI/cockpit usage today) are unaffected.
- `src/app/teaching/lab_cockpit_dock.cpp` ignores `saveSession()`'s false
  return (disk-full leaves the in-memory session newer than disk without a
  surfaced message). The dock is outside this track's write scope
  (`src/app/**`); noted for the app track.
- `listSessions` calls `entry.is_regular_file()` (throwing overload) — a
  file deleted mid-scan could throw `filesystem_error`. Not reachable
  deterministically in tests; left as-is (the surrounding code is
  exception-free by convention, but changing every call is churn without a
  reproducible oracle).

## Environment notes

- `cc1plus: warning: /usr/include/qt6/Qca-qt6/...: not a directory` is
  pre-existing noise on every compile.
- `test_lab_pack_boundaries_r4` needs `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`
  (libodbc from the SDK root); the teaching/lab_runtime/cockpit suites do not.
