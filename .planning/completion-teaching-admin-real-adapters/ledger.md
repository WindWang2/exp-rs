# Goal-Loop Ledger — completion/teaching-admin-real-adapters

Shared `.goal-loop-ledger.md` is NOT committed (per campaign rules); this per-track
record lives under `.planning/completion-teaching-admin-real-adapters/`.
Oracle definitions: see `oracle.md` in this directory.

## Execution timeline (one attributable change per round)

Baseline R0 (2026-09-24): worktree from origin/master `e4904cd3c`; configure
`dev-default` + `-DCMAKE_PREFIX_PATH=$HOME/pwb-sdks/root/usr`; narrow target
`test_teaching_admin_core` builds and is GREEN on clean master
(85 assertions / 10 cases). RED/structural evidence for G1–G7 recorded in oracle.md.

| Round | Change (single, attributable) | Verification |
|---|---|---|
| R1 | `grader_cli_adapter` + additive row fields (`grader_digest`, `top_deduction`, `unavailable_reason`, report `unavailable` counter + CSV columns); fake CLI helper `test_teaching_fake_grader_cli`; 4 new TEST_CASEs | target GREEN 151/14; commit `35b9cb17b` |
| R2 | dock `onRunBatch` rewired to `cliGradeCallable`+`resolveGraderCli`; fake pass/80 lambda deleted; objectNames; new offscreen widget target `test_teaching_admin_dock_smoke` | smoke GREEN 16/2; commit `fdcb28569` |
| R3 | `operator_catalog` reads real capability sidecars; empty registry now fail-closed (validateLabSpec empty-set semantics flipped from skip to flag); dock operator allow-list replaced by registry label | GREEN 177/17; commit `d4c2634d5` |
| R4 | validateLabSpec version contract fail-closed (schema pinned `sicnu.labspec.v1`, spec_version 1–3, `offline_required`), dangling/unsafe refs typed when repoRoot provided; dock+preflight pass root | GREEN 190/19; commit `1fd078686` |
| R5 | validatePackDocument actually uses repoRoot (post-resolution `path_escape`, provenance tier validation, `missing_sha256`); inventoryPacks digest/byte pins with tier strength | GREEN 222/21; commit `1fcd69793` |
| R6 | `student_projection` (masked student view + `assertNoAnswerLeak`); dock B tab logs both projections | GREEN 244/23; commit `8295b0321` |
| R7 | bundle builder platform twins (pure `bundleBuilderRequest`), `inspectBundleManifest` + version-pin cross-check in `verifyOfflineBundle`, `checkLabPackDrift` (gen_lab_packs.py --check) + dock drift button | GREEN 296/27; commit `c693122b4` |
| R7.1 | cli callable no longer overwrites row lab_id (backfill by orchestrator); opt-in real-CLI grading lane ([real-cli] tag) | GREEN 296/28 |

## Defects caught by the loop (fixed in-round)

1. R2 smoke: QFile not flushed/closed before the grader subprocess read the
   artifact → fake CLI answered "unreadable" → typed error row (the pipeline
   worked; the test fixture was wrong). Fixed by scoping the writer.
2. R6: `maskAnswers` iterated `v.toObject()` temporaries → iterator UB/SIGSEGV.
   Fixed by materializing the object.
3. R6 mutation oracle kill: original leak oracle (full-claim quoted match only)
   was beaten by mutation (2) — paraphrased claim fragment in a step title
   passed as clean. Strengthened to whitespace-token 4-gram window detection;
   all four mutations now die (teacher_only_field re-add, claim excerpt,
   unmasked param value at position, grading path in note).
4. R7 tests: same flush trap for fake verifier/foundry scripts (scoped), and a
   wrong `arguments.last()` assertion (script is arguments[1] for cmd.exe /c).
5. QStringLiteral cannot wrap constexpr `const char*` (R5 build error) →
   QString::fromLatin1.

## Mutation / adversarial oracles

- `grader_exit_verdict_mismatch` (R1): a "broken authority" lab id in the fake
  CLI exits 0 with a fail transcript; the adapter refuses it. Killing this
  check would fail `[teaching_admin][grader]`.
- leak oracle mutations (R6): four tampered student views, each must produce a
  typed error — see ledger item 3 and `[projection][adversarial]` TEST_CASE.

## Narrow-verification discipline

Only narrow targets were built: `test_teaching_admin_core`,
`test_teaching_fake_grader_cli`, `test_teaching_admin_dock_smoke`
(+ their sicnu_teaching_admin dependency). All builds `--parallel 2`. No full
app/QGIS build, no clean rebuild; configure ran once (+ re-runs are cheap no-ops).

## Known limits (declared in PR)

- The grader CLI subprocess keeps QtCore purity (forbidden-link guard intact);
  the in-process `OutputVerifier` seam remains the same authority behind the CLI.
- Version pin in `verifyOfflineBundle` is a metadata cross-check; digests stay
  with the canonical verifier script (no second verifier).
- teaching_admin's pack validation mirrors the `sicnu.lab-pack/1` authority's
  tier semantics without linking `sicnu_agent`; consolidating the two parsers
  into a Qt-free leaf is future work beyond this track's minimal-delta rule.

## Adversarial review round 1 (independent reviewer) → fixes

Verdict after full-diff review: PROCEED with 1×P1 + 4×P2 (all fixed in
commit "fix(teaching-admin): adversarial review round 1"):
- P1 inventoryPacks dropped the per-input declared-byte sum (declaredBytes==0
  ⇒ withinBudget trivially true). Fixed + regression assertions.
- P2 top_deduction took deductions[0]; now max-weight scan (canonical
  lab_batch_runner definition).
- P2 runScript ignored QProcess::exitStatus; crashed children could read as
  exit 0. Added crashed flag, plumbed through grader/bundle/drift adapters
  (crash ⇒ unavailable/unverifiable, never a verdict).
- P2 pack validator weaker than authority: added missing_role / backslash_path
  / missing_bytes (committed fixture).
- P2 fake-CLI helper had no CMake dependency from its consumers →
  add_dependencies on both test targets.
- P3s: CSV csvSafeCell twin, integral spec_version, "***"-literal guard,
  sortKeys on GraderCliGrade::toJson.

Re-review: requested from the same reviewer (fix verification + final verdict).

## Re-review (same reviewer) — READY / PROCEED, no blockers

All P1/P2 fixes verified (incl. static check: all 19 committed packs / 111
inputs conformant, zero violations). Adopted P3 recommendations in the final
commit:
- fake CLI "CRASH" lane: valid transcript then SIGSEGV → unavailable/
  grader_crashed, score<0 (found+fixed a real bug while landing it:
  unavailableGrade left started=false for the crash path).
- BAD case now carries two deductions (a1 w10, a2 w60) → locks max-weight
  top_deduction (a2).
- spec_version integral check via std::floor (no UB for huge magnitudes).
- missing_bytes aligned to authority (<0 fails; a 0-byte committed fixture
  stays legal at validation and is caught by the byte pin at inventory).
- scrubbed the username-bearing local path from the ledger.

## Final key-oracle evidence (two consecutive full passes)

- test_teaching_admin_core: 309 assertions / 28 cases — GREEN ×2
- test_teaching_admin_dock_smoke: 16 assertions / 2 cases — GREEN ×2
- git diff --check origin/master...HEAD: clean
