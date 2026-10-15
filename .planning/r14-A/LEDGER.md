# R14-A Ledger

BASE_SHA f72f0c117 → branch hardening/r14-A-science-processing (worktree ../exp-rs-r14-A-science-processing).

## Commits

| # | SHA | WP | Content |
|---|---|---|---|
| 1 | 8d2c0dc52 | A1 | temporal index DN probe positive-only (#856 parity) + parity test |
| 2 | 836558ad5 | A2 | local RX diagonal zero-variance → SingularBackground + kernel test |
| 3 | 532c8ca53 | A3 | sam_classify per-band sentinel wash + hetero test (+A4's test file) |
| 4 | 0c3647582 | A4 | continuum_removal per-band wash + NaN tags |
| 5 | f5ef98cd7 | A5 | radiometric_qa NoData wash (FlagNotFinite not FlagNegative) + test |
| 6 | 29e8577b2 | A6 | image_fusion per-band read-boundary wash + NaN convention + 2 tests |
| 7 | f05185039 | docs | planning whitelist + ledger docs |
| 8 | 500dfb923 | review-r1 | HPF halo wash (F1 P1), probe-test undeclare (F2 P1), sam refs wash (F3) |
| 9 | 50b3f1b5b | A7 | 4× OBIA CSV stage-then-publish; kmeans RNG seed pin; 2 tests |

## Audit rounds (read-only Explore subagents; evidence-based, no patches)

1. NoData/NaN consistency sweep over src/operators/rs + algorithms → WPs A3–A6 + the per-band-correct safe list (~40 operators verified correct; see audit output distilled into ORACLES/matrix).
2. Spectral/band-math numerics (indices, AST, SIMD, SAM/SID, RX, unmixing, resampling, chunk invariance) → WP A1, A2; everything else verified safe with pinned tests (division-by-zero, integer overflow, unknown-variable rejection, singular covariance, zero-norm, #801/#856 scale seams).
3. Classification accuracy + OBIA determinism/atomicity → WP A7 (CSV atomicity, kmeans seed); kappa/confusion/dtype-escalation/remap/label-sidecars/segment determinism verified safe and pinned (F3 hash-order and F4 0/1-based convention recorded as observations only).

## Independent review

- Round 1 (fresh-context reviewer agent): FAIL — F1 HPF halo regression (P1), F2 fake-oracle temporal test (P1), F3 refs sentinel (P3). All fixed in 500dfb923. F4/F5 pre-existing observations recorded.
- Round 2: pending build/test evidence.

## Dedup against open PRs (re-verified live)

- #1504 (geodata durability): zero file overlap; my only shared-file touch is 4-line .gitignore whitelist (not in #1504's file list).
- #1505 (security/concurrency): zero overlap; temporal_cube.cpp NaN-composite fix belongs to it (out of my writable scope).

## Resource discipline record

- All builds/tests under flock /tmp/exp-rs-r14-global-build.lock, -j2/-j1, QT_QPA_PLATFORM=offscreen; A queues behind B/C tracks' builds (observed live contention, waited by design).
- Build uses worktree-local build-r14-A; Catch2 source reused read-only from a sibling worktree's FetchContent cache (network fetch impossible on this host; documented in BASELINE.md).
