# LOOP_LEDGER — R24-C (track-local; the repo-shared .goal-loop-ledger.md is NOT written by this track)

| Round | Change | Verification | Result | Next |
|-------|--------|--------------|--------|------|
| 1 | Recon: fetch/PR/issue/code map; worktree+branch created; planning docs written; configure launched | git fetch OK; 0 open PRs; explorer reports ×3 cross-checked with direct reads | PASS | T1 build baseline |
| 2 | Build baseline: vcpkg tree copied from idle main build-dev (identical manifest) + VCPKG_MANIFEST_INSTALL=OFF; configure needed +flex/bison paths (BISON/FLEX_EXECUTABLE) | cmake configure exit 0, build.ninja 91MB | PASS | RED build of test_r24_c_workspace_charts |
| 3 | #1699 clock gates: 9 sites in 8 files converted to wallClockBudgetMs / clamped-regime windows with includes | source edits complete; compile+run pending build | PENDING GREEN | build & run each file's tests |
| 4 | Pi suite honesty (found during O-C1 baseline): 21/41 false-FAIL on Windows checkout — 2 defect classes: (a) CRLF-fragile text/byte assertions (bridge_parity F-PI-1 regex, knowledge budget byte count), (b) spawn EFTYPE shebang limitation without canary in 17 tests (mcp_bridge ×15, no_drift ×1, r18_c_idnull ×1, r18_c_timeout ×1) | node --test pi: 41 tests, 20 pass, 0 fail, 21 skip, exit 0 (was 18 pass / 23 fail) | PASS (run 1; ×2 at close) | keep as WP5 evidence |

