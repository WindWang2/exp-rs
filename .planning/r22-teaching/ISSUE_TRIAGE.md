# ISSUE_TRIAGE (source-level verified 2026-10-25 @ 911e0b17a)

| Issue | Class | Evidence (source read) | Action |
|-------|-------|------------------------|--------|
| #1694 P1 batch checkpoint digest misses rubric/lab content + grader identity; versions hardcoded "ui-1" | OPEN_UNFIXED | src/teaching_admin/batch_assessment.cpp:26-44 (digest input = 6 fields only), :594-629 (adoption keyed on digest only); src/app/teaching_admin/teaching_admin_dock.cpp:394-395 (`ui-1` hardcoded) | FIX (F0) |
| #1689 P2 cross-student leak guard not on production path; only `student_` prefix IDs | OPEN_UNFIXED | src/teaching_admin/feedback_pack.cpp:41-73 (regex `student_[A-Za-z0-9_-]+` only); src/app/teaching_admin/teaching_admin_dock.cpp:605-633 (`onBuildFeedbackAndSummary` never calls assert) | FIX (F1) |
| #1688 P2 bundleBuilderRequest Windows cmd.exe /c injection, no arg validation | OPEN_UNFIXED | src/teaching_admin/script_adapters.cpp:381-425 (no charset check on version/outDir/buildDir; Q_OS_WIN branch uses cmd.exe /c) | FIX (F2) |
| #1678 P2 recordAnswer accepts NaN/Inf; jsoncpp writes null → session unloadable | OPEN_UNFIXED | src/lab/session_state.cpp:356-363 (only has_value), :454-455 (serialize w/o finite check), :663-671 (parse rejects null) | FIX (F1) |
| #1558 P1 two always-true issue-regression assertions | FIXED_OPEN | fixed by commit 0847c5ec1 (2026-10-19) "fix(tests/test_e2e_open_issues.cpp): resolve full-review findings #1558"; current file: #799 case has 50× waitForTask+terminal assertions, #784 case has full gate-recipe assertions; grep finds no `size() >= 0` / `CHECK(true)` remnants | NO ACTION (do not duplicate; note in PR body) |

Dedup check: none of the 5 open R21 PRs (#1673-#1677) touch
batch_assessment.*, feedback_pack.*, script_adapters.*, session_state.cpp → no fix-in-flight duplication.
