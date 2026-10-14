# R13-H PR / Issue / Review De-duplication Matrix

| Issue / PR | Component | Finding / Claim | Verified on HEAD (892295685e) | Owner | Red Test / Oracle | Resolution Strategy |
|---|---|---|---|---|---|---|
| PR #1483 | Science numerics | Float/double numerics & algorithms | Merged | R12-A | N/A | Off-limits; do not touch. |
| PR #1484 | Agent / IPC | Agent workflow runtime & budgets | Merged | R12-C | N/A | Off-limits; do not touch. |
| PR #1496 | Model runtime | OTB/IO lifecycle & VRAM ledger | Merged | R12-F | N/A | Off-limits; do not touch. |
| PR #1497 | Verification evidence | Grader, faultlab, preflight, suitability dataset facts | Merged | R12-E | N/A | Off-limits; do not duplicate. |
| Issue #1465 | ScienceContext / broker | Cache key drops typed failure cause | Partially fixed in 9052cb5d82 | R13-H (WP-10) | O10: test_science_context_broker | Verify failureDigest keys cache properly on status change. |
| Issue #1466 | Suitability | Integer goal fail-open on string values | Fixed in suitability_goal.cpp | R13-H (WP-14) | O13: test_suitability_adversarial | Keep suitability read-only; lock down contract with regression tests. |
| Issue #1447 | JSON / jsoncpp | Untyped jsoncpp access throwing Json::LogicError | LIVE in planner/json_util.h | R13-H (WP-01/04) | O1: test_scientific_planner_schema | Add isString() guards before asString() across planner envelope checks. |
| Issue #1450 | Single authority | SHA-256 and JSON canonicalizer drift | Audited | R13-H (WP-02/10) | O2/O10 | Use authoritative canonicalizeJson and SHA-256. |
