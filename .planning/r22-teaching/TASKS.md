# TASKS / ORACLES / RISK — r22-teaching round 1

## Work packages (each = one RED→GREEN→GREEN×2 loop + small commits)

- [x] WP0 startup inventory + planning docs (BASELINE/ISSUE_TRIAGE/OWNERSHIP/this file/PR_COLLISION_MATRIX)
- [ ] F0 #1694: checkpoint digest must cover grading authority identity:
      rubric/lab rules CONTENT hash + grader CLI identity; dock stops hardcoding "ui-1";
      digest mismatch → full re-grade with explicit log line.
      Oracle: unit test — run batch with rules file R1 (partial run), rewrite rules content (same
      path/lab id), re-run → resumed==0, all rows re-graded; grader binary content change → same.
      Also: report/CSV carry real versions (no "ui-1").
- [ ] F1a #1689: leak guard on the production path + numeric/Chinese ID detection.
      Oracle: unit test — numeric-ID cohort where row message mentions another numeric ID
      → guard errors and pack is blocked in onBuildFeedbackAndSummary-equivalent code path;
      no false positive on own ID / unrelated numbers.
- [ ] F1b #1678: recordAnswer rejects non-finite numerics (lab.session.field); sessionToJson
      refuses/writes-safe non-finite (belt+braces); sessionFromJson rejects non-finite.
      Oracle: round-trip test — NaN/±Inf answer rejected at record; no `"numeric":null` byte can appear;
      crafted doc with non-finite → parse failure typed (not a null round-trip).
- [ ] F2 #1688: bundleBuilderRequest validates args (version whitelist [0-9A-Za-z._-], paths reject
      cmd metacharacters & quotes); Windows test constructs request and asserts refusal on payloads.
      Oracle: unit test — `1.0&calc`, `C:\R&D\out`, `%TEMP%` refused; legal args still build.
- [ ] F3 (review-only this round): #1674 verifier wiring cross-check → CROSS_TRACK_FINDINGS.md.
- [ ] WP-final: build + targeted tests ×2, R1/R2 independent reviews, docs, push, PR.

## Final acceptance oracles (track charter §7)

1. rubric/lab/grader changes force re-grade; report versions are real and traceable.
2. cross-student isolation enforced before any pack output; NaN/Inf never poisons a session;
   Windows untrusted args cannot become shell commands.
3. evidence-typed verification outcomes preserved (no True/False-only APIs introduced).
4. E2E student→batch→feedback loop reproducible, no silent false pass (covered by existing
   r21-E tests staying green + our new regression tests).
5. cross-track dependencies documented; no out-of-ownership edits.

## RISK_REGISTER

| risk | mitigation |
|------|-----------|
| Windows host: flock unavailable in Git Bash | mkdir-poll lock fallback around builds; -j2 cap; record in ledger |
| full-tree build slow (~15k files) | reuse preset flags; build only affected targets first, full later |
| #1696 master CI compile failures (missing <algorithm>, availability.h) | may hit locally too → fix inside OUR files if the missing include is in our touched sources only; otherwise record NOT_EXECUTED/known-baseline-red |
| tests/CMakeLists.txt append-only conflicts with #1674 | single small block appended at end; re-check PR files before push |
| review subagents may not reproduce env | reviews are diff-based, adversarial; tests run in main session only |
