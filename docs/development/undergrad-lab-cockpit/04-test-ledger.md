# Test ledger

| # | Case | Tag | Result |
|---|------|-----|--------|
| 1 | curriculum→course VM | teaching/course | PASS |
| 2 | lab→timeline | teaching/timeline | PASS |
| 3 | prereq blocked/warn/ready | teaching/readiness/prereq | PASS |
| 4 | autonomy allow/deny/downgrade | teaching/autonomy | PASS |
| 5 | human-only step | teaching/timeline/human | PASS |
| 6 | unknown operator | teaching/readiness/operator | PASS |
| 7 | missing dataset | teaching/readiness/pack | PASS |
| 8 | scientific conflict | teaching/readiness/sci | PASS |
| 9 | indeterminate ≠ pass | teaching/feedback | PASS |
| 10 | grader feedback | teaching/feedback/grader | PASS |
| 11 | session save/reload | teaching/session | PASS |
| 12 | corrupted session fail-closed | teaching/session/failclosed | PASS |
| 13 | offline mode | teaching/offline | PASS |
| 14 | offscreen GUI smoke | — | DEFERRED (known limit) |
| 15 | beginner/expert same truth | teaching/mode | PASS |
| + | teaching mask | teaching/timeline/mask | PASS |
| + | real-repo lab15 e2e | teaching/e2e/lab15 | PASS |

Binary: `build/test_teaching_lab_cockpit` — 109 assertions, 16 cases, all passed.
