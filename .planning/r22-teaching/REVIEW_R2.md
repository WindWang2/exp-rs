# REVIEW_R2 — r22-teaching (independent adversarial subagent #2, 2026-10-26)

Scope: commits 1610c9b75 + 70afbb6a8 + 572068233 vs master 911e0b17a, probing in particular
bugs INTRODUCED by the R1-fix commit. R1's conclusions were not assumed.

## VERDICT (as delivered): REJECT — 1xP1 (a real regression introduced by 572068233), 1xP2 residual, 3xP3

| # | Sev | Finding | Disposition in 318b7d0c8 |
|---|-----|---------|--------------------------|
| 1 | P1 | Empty-cohort sentinel `QRegularExpression()` is VALID with an empty pattern matching the empty string → every roster-less `assertNoCrossStudentLeak` fabricated a foreign-id error (test_teaching_admin_core.cpp:387 `leak.ok` breaks) | FIXED: gate on `pattern().isEmpty()` too; new regression test pins the 2-arg clean case; core back to 35/37 baseline |
| 2 | P2 | Residual: when the console's repoRoot ≠ the CLI's compile-time SICNU_SOURCE_DIR and no SICNU_LAB_RULES_DIR is set, the fingerprint may hash a different rules file than the CLI reads (installed-layout window) | ACCEPTED + DOCUMENTED (CROSS_TRACK_FINDINGS #4): proper closure = transcript carries `rules_path` (CLI/verifier owners' contract); not reachable in the source-tree deployment this console ships in |
| 3 | P3 | Direct-file branch fingerprinted ANY existing file (CLI only treats `.rules.json` suffixes as direct) — decoy file could pin the digest | FIXED: suffix gate mirrors resolveRulesPath; `missing:` typed marker for absent direct paths; decoy test added |
| 4 | P3 | GUI-thread full-file hashing (up to ~100 MB grader binary) — one-off freeze regression vs the previously bounded sampler | FIXED: fingerprint + identity + label derivation moved into the batch worker thread; dock parses authored docs on the GUI thread and passes them in |
| 5 | P3 | Tests: vacuous legacy-prefix assertion; env-dependent lab-id tests (inherited SICNU_LAB_RULES_DIR); stale TEST_RESULTS record | FIXED: message pinned (contains student_bob); EnvGuard hermeticity; TEST_RESULTS refreshed (this file set) |

Verified sound by R2 (negative results recorded): regradeTraceability field stripping vs the
full toJson set; checkpoint schema-evolution fail-closed + artifactStamp interplay; the
Unicode-boundary single-regex scan for owner-repeat / prefix-overlap / metacharacters / dedup
determinism; empty-repoRoot refusal has no real caller breakage; TOCTOU only in the A→B→A
restore cycle (accepted residual); #1678 both boundaries.

## Post-fix verification (318b7d0c8)

- test_r22_teaching_batch_regrade 183/183 ×2 (10 cases)
- test_r22_teaching_guard_hygiene 107/107 ×2 (8 cases)
- test_teaching_admin_core 35/37 — the SAME 2 pre-existing host failures as master
  (stash-baseline-verified earlier): :531 grader_crashed (MSVC abort()=exit 3, not a signal)
  and :1634 python pin probe
- test_teaching_admin_checkpoint_stamp_r16_c_t06 85/85
- dock TU: full-chain 1624/1624 exit 0 + post-R2 incremental 1212 TUs exit 0
