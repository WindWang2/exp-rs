# EVIDENCE — local verification log

## Environment
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-whole-repo-line-review` @ `27b9aa0a63` (origin/master), branch `zcode/whole-repo-line-review`.
- Resource envelope: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, ninja `-j2` (drop to `-j1` if RSS>70% / load>1.5×cores), `QT_QPA_PLATFORM=offscreen`.

## Dedupe baseline capture (Phase 0, no build)
- `gh issue list --state all --limit 250` → 250 issues #595–#945 all CLOSED → `DEDUPE_BASELINE_ISSUES.txt`.
- Historical audit findings read: `.scratch/audit-final/findings.md` (F-001..F-023 georef/classification, 2026-08), `.scratch/audit-final/` sibling files, `.scratch/findings-audit.md` (F-101..F-106).
- `PROJECT.md` (#773–#817 contracts, all DONE), `.agents/ORIGINAL_REQUEST.md` (#848–#882 batches, PRs #837–#847).
- Delta to review from pin: `git log efc5c52f..origin/master` = 23 commits, 65 files, +2588/−458.

## Verification runs
(appended per finding; each entry: finding id, command, exit code, observed output)

## Delta-pass (efc5c52f..27b9aa0a63, 2026-09-13)
All 23 commits' hunks read: SAR ratio/change/flatten (#929/#934 grid preflight + declared-dB refusal — correct), spectral_index scale-always-multiplicative (#945 — reviewed in full-file pass), toolbox_raster_preflight.h (new, clean), ExecutionId wire form (plan_tools), data_manager QAtomicInt dedup (#943 follow-up — correct), split.cpp Result const-fix (correct), local_worker_pool multi-teardown vector (correct — no worker leak), task_center (#930/#942-era paths read in full-file pass), workflow_run_coordinator (#931/#944 — full-file pass), plugin_registry (#928 lock-drop — lock-face pass), ipc_frame (#925/#926 — full-file pass), recipe_catalog/mcp_server/cli_commands/schema_form_builder (#927 userTouched — hunks read). New tests in delta (test_execution_id, test_exprs_*, test_sar_operators, test_schema_form_builder_v2, test_workflow_run_coordinator) reserved for the Phase 5 lens-6 pass. No new findings from the delta; fixes verified in place.

## PR (runbook step 8)
- Branch pushed: `zcode/whole-repo-line-review` → origin (16 review commits on 27b9aa0a63).
- PR: https://github.com/WindWang2/exp-rs/pull/957 — "review: whole-repository line-by-line audit dossier" (GitHub 5xx on first two attempts; created on retry per runbook step 6 discipline, no force push).
- Diff scope verified: 23 files, +3,963/−0, all under `review/` + `.planning/` + `WHOLE_REPO_REVIEW.md`; zero `src/`/`tests/` changes.
- Not merged; no remote issues created; no CI awaited. Worktree `/home/kevin/projects/rs-studio/exp-rs-whole-repo-line-review` retained until merge.
