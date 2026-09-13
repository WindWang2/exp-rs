# PROGRESS — hyperspectral-spectral-intelligence-10

Append-only log. Newest last within a phase; phases in order.

## Phase 0 (2026-09-13)

* Baseline SHA `7d78059d1a` verified (fetch + ff-only pull; one transient TLS error, retry clean).
* Read: README/PROJECT/CONTEXT/CHANGELOG/CLAUDE/AGENTS/goal-template/command-vocabulary/WHOLE_REPO_REVIEW/ISSUES/READINESS/review DEDUPE+findings; PR list; remote branches; open PRs (none).
* Dedupe: PR #955 (library+priors) mapped as do-not-redo; ISSUES.md H-1/H-2/H-3 confirmed in code (line refs in BASELINE.md).
* Worktree `../exp-rs-hyperspectral-spectral-intelligence-10` + branch `zcode/hyperspectral-spectral-intelligence-10` off origin/master.
* `.gitignore` whitelist block added; `git add -n` verified planning md files trackable (check-ignore -v shows the `!` negation; ground truth = add dry-run success).
* Planning files: GOAL/BASELINE/OWNERSHIP/ARCHITECTURE/CAPABILITY_MATRIX/PLAN/MILESTONES/DECISIONS/PROGRESS/EVIDENCE/REVIEW_LOG/PERFORMANCE/PR_BODY.

## Phase 2/3 implementation (2026-09-13, build round 1 in flight)

* WP-A: `spectral_table.{h,cpp}` (typed artifact, digest, license rule, bounds), `spectral_wavelength.{h,cpp}` (grid helpers), `tests/test_spectral_table.cpp`.
* WP-B: `rs_spectral_reference_input.{h,cpp}` (shared seam); `rs:endmember_extraction` gains `endmembersOut` + payload `endmembersArtifact`; workflow_runtime records `<stepId>.<port>` artifacts on both paths (session/TaskCenter parity, DECISIONS D-1).
* WP-C: `refsRef`/`endmembersRef`/`targetRef` + `libraryPath`(+`libraryMaterials`) on sam/unmix/mf/ace via the seam; provenance echo in result payloads.
* WP-D: `mnf_transform.{h,cpp}` (double-precision streaming kernel, digest-verified model artifact); `rs:mnf` rewritten (3-pass row streaming, `transformOut`, SNR payload); `rs:mnf_inverse` new (raster + spectrum modes, `errorOut` dropped-mass RMSE, wavelength restoration).
* WP-E: `SpectralUnmixing::unmixFcls` (Lawson-Hanson NNLS + penalty sum-to-one, collinearity/zero-norm refusals); `method` param on rs:spectral_unmixing.
* WP-F/G: `rs:spectral_band_select` (bands/wavelength window/exclude ranges, nm normalization), `rs:library_select` (materials/window/sensor projection, near-duplicate QA, license report).
* Docs: ADR 0148; tests for table/wavelength, MNF kernel, FCLS (appended), pipeline flow.
* Commits to follow after build verification (ninja configure auto-picks up new CMake entries on next build).
