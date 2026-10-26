# CROSS_TRACK_FINDINGS — r22-teaching round 1

## To infra lane (07 / build)

1. **#1696 blocks lab tests on MSVC**: `src/lab/session_state.cpp` uses `std::sort` (line ~826)
   with only `<utility>`. Our #1678 behavioral fix in the same file was verified with a
   TEMPORARY LOCAL include (never committed). Once infra's `<algorithm>` fix merges,
   `tests/test_r22_teaching_guard_hygiene.cpp` (#1678 sections) builds and passes on MSVC
   out of the box — no rework needed in our files.
2. **#1697 (build_offline_bundle --version path traversal)**: our `validateBundleBuilderParams`
   (src/teaching_admin/script_adapters.{h,cpp}) now refuses `&|<>^%!\"` and non-`[0-9A-Za-z._-]`
   version strings at the C++ adapter layer, on BOTH platform twins — the adapter side of the
   interface the 07 track's script-side `--version` fix needs. The scripts themselves remain
   07's ownership; we did not touch them.
3. **vcpkg manifest ABI storm on this host (2026-10-25)**: a manifest-mode configure against
   the shared `build-dev/vcpkg_installed` decided the tree's ABI hashes were stale and began a
   from-source rebuild, deleting `opencv4`+`protobuf` from the shared tree mid-run (killed at
   opencv4). Repaired by completing `vcpkg install` (8.5 min, binary cache updated). Root cause
   of the ABI shift unexplained (first run had refreshed `compiler-file-hash-cache.json`).
   Recommendation: R22 tracks on this machine should consume the installed tree CLASSIC-MODE
   (explicit `*_DIR`/CMAKE_PREFIX_PATH, no vcpkg toolchain) or use a per-track VCPKG_INSTALLED_DIR.

## To r21-E (#1674, teaching-verification — read-only for us)

- Our #1694 work deliberately composes with, not duplicates, #1674's verifier wiring:
  `gradingRulesFingerprint` fingerprints the RULES AUTHORITY the grader CLI reads; #1674's
  lab_verify_composer/output_verifier evidence stays the transcript-level truth. No shared
  files edited (collision matrix held).
- `BatchAssessmentReport::checkpointInvalidated` is a NEW additive field in the published
  batch JSON; `loadBatchReportFromFile` ignores unknown fields (verified by reading), so
  #1674's consumers are unaffected.

## To teaching lane successors

- `gradingVersionLabels` prefers an authored `"version"` member; authors of rubric/lab spec
  documents should START stamping a `version` string so reports show `2026.10` instead of
  `fp:<hash12>` fingerprints.
- Known limitation: when `labIdOrRulesPath` is a lab ID (not a file), the CLI's internal
  resolution (lab pack data) is invisible to the digest; grader CLI identity covers binary
  changes but not data-pack updates. A future CLI `--print-authority-fingerprint` flag would
  close this; recorded as follow-up, not blocking.

## To CLI / verifier owners (r21-E #1674 successors)

4. **Transcript should carry `rules_path`** (R2 finding 2 residual): the console's rules
   fingerprint resolves `<id>.rules.json` through env `SICNU_LAB_RULES_DIR` + the repo root it
   runs from; the CLI's final search dir is its compile-time `SICNU_SOURCE_DIR`. When the
   console runs from a tree different from the grader's build tree and no env dir is set,
   the fingerprint may hash a different file than the CLI reads (stale-adoption window,
   installed layouts). Exposing the rules path actually read (e.g. in the
   `sicnu.lab.grade/1` transcript, `LabGradeResult::rulesPath` already exists in
   output_verifier.cpp:2207) and stamping it into the batch checkpoint closes this exactly.

## From #1558 triage (no action)

#1558 fixed by 0847c5ec1 (2026-10-19); assertions in tests/test_e2e_open_issues.cpp are real.
Issue can be closed by its owner.
