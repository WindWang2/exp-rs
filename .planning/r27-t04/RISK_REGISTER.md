# R27-T04 Risk Register

| ID | Risk | L | I | Mitigation |
|---|---|---|---|---|
| R1 | Removing sync `delete` in #1745 leaves tool alive longer → rubber-band residue or memory growth | M | L | deactivate() already hides+resets band; deleteLater frees at next event loop pass; test asserts no residue via re-invoke loop |
| R2 | #1746 guard changes curve output for orphan ids (behavioral) | M | M | Orphans previously produced OOB garbage; new behavior = skip + status message; test pins skip semantics; classDefs-sync paths (rebuild/merge) fixed so orphans stop being produced |
| R3 | #1747 preflight could refuse legitimate saves (store temporarily unavailable) | M | H | Preflight uses the *same* condition as serializer refusal (v3 seen && !liveStore && !cache); refusal preserves file and shows actionable diagnostic; test covers both refuse and healthy-save paths |
| R4 | Engaging private-network policy in GUI breaks intranet labs / localhost-based tests | M | M | Escape hatch env `SICNU_ALLOW_PRIVATE_NETWORK=1` (documented in code + PR); offline gate semantics untouched; run app-test suite to catch regressions |
| R5 | Async magic wand changes ROI application timing (queued apply) → double-apply on rapid clicks | M | M | Generation stamp per tool; superseded results dropped; only latest click's geometry applied |
| R6 | #1794 refactor of a 200-line function risks numeric drift | M | M | Keep exact accumulation order/logic; worker returns the same Curve vector; golden-value test comparing old/new means on a synthetic raster |
| R7 | tests/CMakeLists.txt append-only conflicts with siblings | M | M | Unique end-block `# ── R27-T04` markers; 3-way re-check before PR; never touch others' blocks |
| R8 | Build resource contention on shared host (5 worktrees) | H | M | flock `/tmp/exp-rs-build.lock`; `-j2` cap; RAM/load check before each build |
| R9 | GUI offscreen tests unstable under parallel ctest | M | L | `-j1` for my test invocations; QEventLoop discipline with timeouts in tests |
| R10 | jsoncpp `stackLimit` behavior differs between parse entry points | L | L | Use exact house pattern (lab_cockpit_dock/lab_operator_launch); test with 10k-deep nesting |

Blockers: none currently. External: no macOS host (F6 test NOT_EXECUTED), no remote CI reliance.
