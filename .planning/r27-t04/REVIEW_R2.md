# R27-T04 Review R2 (fresh adversarial pass)

Reviewer: second isolated subagent; audited the R1-fix commit (2f52dcc4e4), the follow-up (8f382571bd), and re-checked compat/negative paths + every issue's original ask against the delivered code. Code-reading only (coverage statement below).

## Verdict
**No S0/S1/S2 remain.** "The branch is sound." One genuine defect had been introduced by the R1 fix commit (dead staged==src guard) — independently identified by R2 AND pre-fixed on-branch in 8f382571bd, whose replacement R2 verified live.

## Findings and disposition (all S3)
| # | Finding | Disposition |
|---|---|---|
| S3-1 | Backwards comment in the mission backup sweep (behavior correct, comment stale) | **FIXED** — comment corrected |
| S3-2 | The R1-fix staged==src guard was dead-on-arrival (canonical of a not-yet-created file is always empty) | **FIXED** in 8f382571bd (string-space guard from the resolved dest dir); R2 verified live |
| S3-3 | No regression coverage for the two R1 S2 hardenings (symlinked-dir dest==src; selectCogHref deep nesting) | **FIXED** — both pinned (export suite +28→28 assertions incl. symlink case; STAC suite 27→29 incl. 70-deep refusal) |
| S3-4 | searchDropped does not restore m_searchButton — unreachable in-app (dialog is stack-scoped per exec(); the only generation bump coincides with its destruction) | Ledgered for the owning track (no in-reach path) |
| S3-5 | Two early commits' messages under/over-describe their content (hurt bisection) | Accepted as-is: content complete and correct on the branch; history rewrite of a 20+-commit branch rejected as risk>benefit; noted in PR body |

## R2 focus-area verdicts
- A1 selectCogHref hardening: PASS (transport already pins stackLimit=64; catch covers jsoncpp exception types; {} → honest dialog warning)
- A2 symlinked-dir dest==src: PASS at HEAD (empty-canonical path analyzed; Windows separators/casing consistent; staged path built in canonical dir, publish rename safe)
- A3 staged==src guard: live at HEAD (also closes the symlinked-dir variant)
- A4 backup sweep: PASS (same-ms collision impossible by construction; QStringView bounds-safe)
- A5 shared-QPointer self-guard: PASS (watcher child-of-tool, deferred delete safe mid-emit, no re-entry; double deleteLater idempotent; strictly better than pre-fix behavior)
- A6 OBIA resetIdentity: PASS (behavior-preserving; zero readers of the removed flag; gt/path/layer consistency maintained)
- B7 downgrade corner (RO store + unparseable block): correct-by-spec — Save/Save-As/close-save all refuse (Discard possible), matching issue #1747's own suggested fix; ordering before mission-ref mutation verified
- B8 More-button settlement: PASS (all three unavailable reasons terminal; double-hide unreachable/harmless)
- B9 recompute epoch: PASS (entry bump precedes early returns; landings self-validate; out-of-order landings rejected; UI keeps previous curves between supersede and landing)
- B10 original-issue cross-check: **all 13 issues delivered as asked** (table in reviewer output; #1765 delivered via process policy engagement with justification)
- B11 hygiene: CLEAN (no debug prints/TODO/dead code added)
- B12 structure: no oversized file diffs; commit-mixing only in the flagged early commits (S3-5)

## Post-fix verification
Rebuilt affected targets; final full double run 13 suites × 2 all exit 0 (incl. the strengthened export/STAC suites); bench 3/3 green (slot p95 ≤ 1.24 ms).

Reviewer coverage limits: code reading only (nothing built/run by the reviewer); macOS/Windows/remote-network runtime not exercised; out-of-whitelist surfaces (src/python saveProject bypass, src/geospatial refusal text) remain open for owning tracks; .planning docs not audited.
