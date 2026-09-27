# REVIEW_LOG — Track 2: Qt/QGIS teardown lifecycle (R4 Deep)

Independent adversarial review (Phase 5): one read-only subagent (general-purpose) reviewed `git diff origin/master..HEAD` on Standards + Spec axes with evidence-based findings; subagent consumed ~1.5M tokens, 51 tool calls, ~26 min.

## Findings and resolutions

| # | sev | axis | finding (abridged) | resolution | commit |
|---|---|---|---|---|---|
| 1 | P0 | SPEC/STD | `test_georeferencing_session` / `test_workbench_full_shell_lifecycle`: helper include landed at EOF (their last `#include` is at file end) → TU does not compile; first-use before declaration | include moved into top include block; both targets rebuilt (binary `strings` shows listener marker, old defense absent); double-run green ×2 | review-fix commit |
| 2 | P0 | SPEC | Rows 9/26 "green ×2" evidence came from stale binaries still carrying the retired `_Exit` defense (never recompiled); neither was selected by the Oracle regex nor caught by the text gate | same fix as #1; genuine rebuild + marker check + double-run; EVIDENCE §3 correction appended; both rows' verdict text now states this honestly | review-fix commit |
| 3 | P1 | SPEC | `.gitignore` (+5, convention-conformant) and `.goal-loop-ledger.md` (rewritten tracked file of a prior track) are outside the declared whitelist | kept deliberately: `.gitignore` hunk follows the repo's per-track whitelist convention (required to ship planning artifacts); ledger rewrite follows the tracked-ledger convention (`git log -- .goal-loop-ledger.md` shows prior tracks committing ledger updates; history preserves the prior track's content). Both explicitly declared in the PR body | this branch |
| 4 | P1 | SPEC | RETIREMENT row 31 text described a FastExitListener/heap-app mechanism that contradicts the actual fix (scoped stack app, own main, no listener) + unpinned commit | row rewritten to the true mechanism, commit pinned 9c3bc4bb2 | review-fix commit |
| 5 | P1 | STD | `test_layout_tools.cpp` kept the masking-era comment + empty anonymous namespace, instructing readers to reintroduce `_Exit` | replaced with the standard retirement rationale; empty namespace removed | review-fix commit |
| 6 | P2 | STD | `heapQ*` fallbacks can return nullptr on app-kind mismatch | fail-fast `qFatal` on mismatch in `heapQApplication` | review-fix commit |
| 7 | P2 | STD | `orderlyTeardown` deletes `instance()` without ownership proof (already fired once on provider_http) | ownership-contract warning comment at the `delete`, citing the provider_http incident | review-fix commit |
| 8 | P2 | SPEC | Gate counted only `::_Exit(` — unqualified `_Exit(` reintroduction invisible | pattern broadened to `"_Exit("` (paren-guarded superset) | review-fix commit |
| 9 | P2 | STD | Listener registered mid-file in `test_qgis_display_manager`; `REQUIRE(true)` tautologies in exit-path cases | registration hoisted to include block; tautologies replaced by observable post-teardown assertions (`QCoreApplication::instance() != nullptr`) | review-fix commit |
| 10 | P2 | SPEC | Lowercase `_exit(` Windows tails un-adjudicated despite completeness claim | explicit out-of-scope note appended to RETIREMENT.md naming them as follow-up candidates | review-fix commit |

Bookkeeping resolved: REVIEW_LOG itself now filled (was skeleton); `ctest-gate-run2.md` tracked; EVIDENCE §9 file count corrected 36→50 (committed diff) with the floors table updated; EVIDENCE §3 correction entry for rows 9/26 added.

## Post-fix verification
- Rebuilt: both P0 targets + layout_tools + qgis_display_manager + exit_path + gate. 
- Double-run green: georeferencing_session ×2, workbench_full_shell_lifecycle ×2 (marker-verified), layout_tools ×2, qgis_display_manager ×2, exit_path ×2, gate ×2 (see EVIDENCE §11).
- Gate re-drill after pattern change: not repeated (pattern is a superset; original drill remains valid — superset only adds matches).

## Verdict: FIX-FIRST → all P0/P1 resolved with evidence; P2s resolved or documented.
