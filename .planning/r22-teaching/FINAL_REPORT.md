# FINAL_REPORT — r22-teaching round 1 (2026-10-26)

Branch agent/r22-teaching-20261025T131928 (base master 911e0b17a), 4 commits:
1610c9b75 (RED + planning) → 70afbb6a8 (fixes #1694/#1689/#1678/#1688) →
572068233 (R1 findings closed) → 318b7d0c8 (R2 findings closed).

## Delivered

1. **#1694 (P1)** — checkpoint adoption now keyed on grading-authority identity:
   rules-content fingerprint (direct *.rules.json path, or lab-id resolved through
   SICNU_LAB_RULES_DIR + repoRoot/data/labs/grading, mirroring the CLI) + grader binary
   identity (path+size+streamed sha256). Rules edit in place / grader swap → digest mismatch →
   full re-grade, published as `checkpoint_invalidated` + console log. Dock version labels
   derived from authored documents (`version` member or `fp:<hash12>`); "ui-1" eliminated.
   Traceability digest unaffected by run metadata.
2. **#1689 (P2)** — cohort-aware leak guard (Unicode word boundaries; numeric ids, Chinese
   names, prefix overlap safe) wired into the production emission path
   (`buildGuardedFeedbackPacks`): leaking packs withheld + accounted; single compiled regex
   per batch (GUI-thread friendly).
3. **#1678 (P2)** — NaN/±Inf numeric answers rejected at recordAnswer AND at
   sessionFromJson (both typed); literal 1e999 poison pinned by test.
4. **#1688 (P2)** — bundle-builder parameters validated fail-closed on BOTH platform twins
   (ASCII version whitelist; cmd.exe active chars incl. , ; = refused in paths and repoRoot);
   illegal input → empty request / typed refusal, never a process.
5. **#1558** — verified FIXED on master (0847c5ec1); no duplicate work.

## Oracle attainment (charter §7)

1. ✅ rules/grader changes force re-grade; report versions real & traceable (tests: rules-edit,
   grader-swap, lab-id-resolution, resume-guard).
2. ✅ leak isolation before any pack output; NaN/Inf cannot poison a session; injected argv
   cannot reach cmd.exe (tests for each).
3. ✅ evidence-typed outcomes preserved (no True/False APIs; additive report fields only).
4. ✅ batch→feedback loop reproducible (checkpoint family 85/85 + guarded emission tests);
   no silent false pass (unavailable/typed rows untouched).
5. ✅ cross-track deps documented (CROSS_TRACK_FINDINGS: #1696 infra include, #1697 script
   alignment, vcpkg storm postmortem, transcript rules_path ask); no out-of-ownership edits
   (tests/CMakeLists.txt + .gitignore appends only, per repo convention).

## Reviews

R1 REJECT (2×P1, 2×P2, 5×P3) → 572068233; R2 REJECT (1×P1 regression, 1×P2 residual,
3×P3) → 318b7d0c8. Both reviewers independent subagents, full source read, counterexample-driven.
Post-R2 all findings closed or documented-accepted.

## Verification summary (Windows/MSVC host; full detail in TEST_RESULTS.md)

- R22 suites: 183/183 and 107/107 assertions, each run TWICE after the final commit.
- Families: checkpoint_stamp 85/85, grader_cli_budget 3/3, r15_E_batch_refusal 62/62,
  rubric_weights 19/19, student_id_authority 23/23.
- test_teaching_admin_core 35/37 — identical to master baseline on this host (2 pre-existing
  env failures, stash-verified): MSVC abort()-is-exit-3 crash simulation + python pin probe.
- test_lab_runtime 24/25 — pre-existing Windows temp-dir remove_all handle race (same at
  RED baseline).
- Dock compile: teaching_admin_dock.cpp TU built twice (full chain + incremental after R2),
  both exit 0.
- NOT run: full-repo sweep, GUI runtime, CI (policy).

## Known limitations / follow-ups

- Installed-layout rules divergence window (R2-2): needs transcript-carried rules_path —
  cross-track ask to CLI/verifier owners.
- Identically-named students textually indistinguishable to the leak guard (documented).
- A→B→A rules restore TOCTOU (accepted; self-corrects on any later edit).
- sicnu_lab_runtime cannot build on MSVC until infra's #1696 include lands (our tests are
  registered and pass under the documented temp-include workaround).
- Next rounds (charter F3-F6): verifier production-wiring cross-check vs #1674, scale
  reproducibility (1/50/1000 submissions), E2E offline teaching loop.
