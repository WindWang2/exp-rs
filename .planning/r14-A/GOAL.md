# R14-A Goal — 遥感科学算法数值/性能收敛（不新增算法家族）

Terminal oracles (all must hold before PR):

1. Every WP below carries a status `FIXED / ALREADY_FIXED / NOT_APPLICABLE / BLOCKED_WITH_EVIDENCE` with file:line evidence.
2. Each fix has RED→GREEN×2 (same test run twice green after the fix, failing before it) with real exit codes logged in TEST_RESULTS.md.
3. All edits stay inside OWNERSHIP.md writable paths; `git diff --check` clean; no other track's worktree touched.
4. Independent reviewer (non-implementer subagent) completes two rounds; P0/P1 findings = 0 at the end.
5. Branch pushed, PR created against master with the mandatory structure; no auto-merge; no CI wait.

## Work packages (from the R14-A brief, mapped to verified defects)

- WP01 census / WP13 metadata-contract: covered by audit agent output + `test_radiometric_vocabulary_contract.cpp` (existing) —
  statuses recorded in ISSUE_REVIEW_MATRIX.md.
- WP02 NoData/NaN 契约: WP-A3 (sam_classify), WP-A4 (continuum_removal), WP-A5 (radiometric_qa), WP-A6 (image_fusion) —
  per-band sentinel wash / blind-spot fixes.
- WP05 指数/波段数学: WP-A1 (temporal index DN probe parity with #856).
- WP06 谱学: WP-A2 (local RX diagonal zero-variance contract).
- WP03/04/07–15: audit coverage recorded in the matrix; targeted fixes only where evidence found (none beyond the above so far —
  the prior rounds #1479/#1483/#1501 already closed the previously filed items in those areas, verified by the audit agents'
  "checked safe" lists).
