# R18-B Oracles

- **Oracle-A 重复性**: every claimed fix ships a minimal failing reproduction (RED) + two GREEN runs after the fix. Unreproducible candidates → FALSE POSITIVE, no patch.
- **Oracle-B 本地验证**: build/CTest/CLI/offscreen results + exit codes → TEST_RESULTS.md; runnable in-track tests must pass; environment-blocked tests recorded as NOT_EXECUTED with reason + compensating verification; never green-washed; PR is not delayed by online CI (issue #1491: master CI has no completed runs since 2026-10-11).
- **Oracle-C 性能**: baseline machine/config/input/repeats/RSS/latency recorded first; comparisons same-input same-config ≥3 runs, hot/cold split; no unmeasured ratios; correctness > speed; output semantics unchanged.
- **Oracle-D 回归契约**: existing user paths don't regress; incomplete-data/no-perm/cancel/timeout/crash-restart/concurrency produce distinguishable states (Fail/Indeterminate/Cancelled — never fake Success); thread-safety, error taxonomy, schema, resource release all evidenced.
- **Oracle-E 所有权**: every file in `git diff --name-only origin/master...HEAD` matches OWNERSHIP.md or carries written cross-track permission; overlap re-scanned on every base change; stop sub-items touching #1513's write set.
- **Oracle-F Review**: ≥2 review rounds by contexts that authored nothing in the fix; P0/P1 + reproducible P2 fixed before next round; unresolved P2/P3 logged with rationale.
- **Oracle-G 交付**: atomic commits; no secrets/big binaries/build artifacts; `git diff --check` = 0; in-track tests complete; `review_pack.py` evidence pack; branch pushed; real PR URL via `gh pr create`. If push/PR denied, keep commits + PR body and report the true reason.

## Quantified self-check matrix (task book §7)

1. Same model+input on CPU: output hash/tolerance + device-selection record across runs; GPU vs CPU compared by task semantics with explicit non-determinism tolerance (no fake bit-exactness).
2. Tile interruption/cancel/failure: published products in legal state, no leaked resources; session-pool concurrency model has mechanical reproduction + fix evidence.
3. Illegal shape/manifest/path/device inputs fail closed; throughput/peak-memory quantified before/after; raw GPU/CPU availability labeled.
4. No R18-A math-kernel rewrite; no new provider framework.
