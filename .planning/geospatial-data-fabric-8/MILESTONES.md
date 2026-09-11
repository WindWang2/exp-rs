# MILESTONES

- M0 Baseline ✅ (this commit): audit + planning docs + worktree from `322dfd3876`.
- M1 Build sanity: configure reuse/verify build, run baseline targeted IO tests.
- M2 Pkg D: `time_normalization` + StacItem UTC + deterministic series + tests.
- M3 Pkg B: identity token + bridge + collector wiring + host installs + tests.
- M4 Pkg C: range-cache hazard fix + concurrent/reset/COG fault evidence.
- M5 Pkg F: GeoParquet write path + certified round-trip + profile upgrade.
- M6 Pkg E: string/datetime axes + EO cube workflow test.
- M7 Pkg H+I: doctor checks + CLI `data identity|cache|stac search` + drift sync.
- M8 Pkg A+G: identity canonical/digest enrichment + credential docs/redaction tests.
- M9 Docs ledger, CHANGELOG, FINAL_REPORT.
- M10 Adversarial review (2 subagents) + remediation + re-tests.
- M11 Final diff inspection, push, PR.

Status updates appended below as milestones complete.

## Status 2026-09-10 (in progress)

- M1 ✔ baseline light IO suites green; pre-existing fixture accept()-deadlock
  and range-cache handler use-after-free (P0) found and fixed.
- M2 ✔ STAC UTC normalization + deterministic series (14/14 stac_client).
- M3 ✔ geospatial identity token + data bridge + collector wiring + host
  installs; geospatial-side tests green (5/5 identity); temporal-wiring test
  and CLI e2e tests await the heavy build.
- M4 ✔ range-cache fault evidence + updateEntrySize hazard fix (13/13).
- M5 ✔ GeoParquet certified round-trip (7/7) + profile upgrade + doc row.
- M6 ✔ multidim string datetime axes + EO cube workflow (9/9); fixed a 7.0
  DimensionInfo toJson drift (axis values were dropped on serialization).
- M7 ✔ doctor cacheability/reproducibility/resampling/multidim-axes checks
  (8/8) + `data identity` / `data cache check` CLI subcommands + e2e tests.
- M8 ✔ docs/io/cloud-credentials.md + certified-formats/stac-interop updates.
- M9 in progress (CHANGELOG/FINAL_REPORT).
- M10/M11 pending (review, rebase on #836, push, PR).
- M9 ✔ CHANGELOG, FINAL_REPORT, TEST_MATRIX, PERFORMANCE, REVIEW_LOG, DOCS_LEDGER.
- M10 ✔ Adversarial review (2 subagents) + two remediation passes; all
  P0/P1/P2 fixed, P3s fixed or justified in REVIEW_LOG.md.
- M11 ✔ Rebased on origin/master (`2a4541319a`), final diff inspected
  (43 files, +3090/−69, no unrelated churn), suites re-verified green.
