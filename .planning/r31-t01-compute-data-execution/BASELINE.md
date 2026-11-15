# R31-T01 Baseline

Captured 2026-11-15 after `git fetch origin --prune`; the execution base is current remote reality, not the older local checkout.

## Repository facts

- Repository: `WindWang2/exp-rs`; default branch `master`.
- `origin/master`: `85ce917bea0ece4df6548a677b97a72ecde8659b`, merged PR #2067 at 2026-11-15T12:00:12Z.
- Local original `master` before worktree creation: `59edc791371ac7c93027f1cdd89aaadb37bfc83f` (#2063); no unpushed local commits.
- One open PR: #2061, head `35f7a6227f58bb60e07f8b0911eb21345744f725`, mergeable=`MERGEABLE`, merge state=`UNSTABLE`; this is not evidence of conflicts. No conversation comments, formal reviews, or inline review threads were returned.
- 48 open issues after filtering out PRs. Relevant issue bodies/comments were fetched into `.scratch/r31-t01-upstream/`; triage is behavior-based, not title-based.
- Six remote branches including master. Four `codex/*` heads correspond to merged #2064–#2067 and are retained merged heads, not automatically active work. #2061 is the sole open non-master development branch.

## Merged baseline to preserve

- #2054: R30-T06 F1/F2 only; the rest of that old desktop plan is not claimed complete.
- #2055 (`8e661a3b`): host-lane CAS, fresh-catalog cache registration, resume guards, verifier authority documentation.
- #2056: R30-T05 Pi fixes; outside this track and not repeated.
- #2057 (`cba8d1f4`): wide/stable evaluation, ROI cancellation/windows, sampling budget, global CC/sieve.
- #2058 (`a06bc0c2`): unified four-lane execution contract, per-invocation cancellation, load identity fields, sampled digest memo, sum-of-feeds working set, streaming tiles.
- #2059 (`affbd228`): VRT post-publish sweep, content-aware staged names, preparation reuse/cancel, void handling, subdataset selection, drift propagation.
- #2060/#2062/#2063: managed Python and approved Python output publication; Python-specific dispatcher behavior must remain intact.
- #2064/#2065: bilingual startup/preferences; #2066/#2067: Pi P0/P1 only. Pi P2 host integration is not part of this track.

Formal GitHub reviews and inline threads for #2054–#2067 were empty at capture; PR #2066/#2067 each have one merge-summary conversation comment. Independent review evidence in PR bodies and repository documents is therefore read separately rather than treating empty formal reviews as no review.

## Instructions and toolchain

- Root `CLAUDE.md` and `.agents/AGENTS.md` read. No additional `AGENTS.md`/`CLAUDE.md` was found under `src`, `tests`, or `.planning` in the new worktree.
- No installed `goal-loop` skill was present in the registered skill listing or repository search; the user's `/goal` fallback is active.
- CMake 4.4.5, Ninja 1.13.2, Python 3.14.9. Preset schema v3 is supported; `dev-default` uses this worktree's `build-dev` with tests enabled.
- Host `cachyos-3080`, 62 GiB RAM; about 33 GiB available at initial inspection. This is a point-in-time reading, not a build measurement.
- `resource_guard.py --help` read. All configure/build/heavy test commands use host lock `$HOME/.cache/exp-rs-agent/host-build` plus `$EXPRS_BUILD`, compile cap 4, CTest cap 1.

## Build/test status

Fresh configure was attempted first, but CMake stopped because OpenCV was not discoverable (`src/processing/CMakeLists.txt:12`); the actual local package prefix was then confirmed from the existing host build cache. The immediate retry did not start CMake because parallel T02 held the canonical host lock, and exited 75 after 45 seconds as designed. The 300-second attempt hit the host task timeout while waiting, and the 1800-second guard wait ended with exit 75, again without starting CMake. A bounded wait-3600 retry is queued in the background as `bash-57n2o6fa`. No build or test has completed yet; no historical binary or another worktree's result is used as evidence for this HEAD. Existing failures will be A/B classified only after base configuration and test discovery.
