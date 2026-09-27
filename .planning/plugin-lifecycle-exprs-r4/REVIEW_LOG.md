# REVIEW_LOG — Track R4 (plugin lifecycle & exprs registry rollback)

## Pass 1 — independent adversarial review (read-only subagent, 2026-09-27)

Scope: full diff `origin/master..HEAD` (base `15e5c66b5`), plus the
implementation seams behind each new suite (`plugin_registry`,
`plugin_loader`, `ipc_channel/frame`, `plugin_host_process_runtime`,
`plugin_discovery/validator/snapshot`, CMake wiring) and the open PRs
#1334/#1335 for claim cross-checks.

Verdict: **SHIP-WITH-FIXES** (P0 ×1, P1 ×2, P2 ×3, P3 ×5). The reviewer
independently verified: the P1-9 non-reproducibility documentation (titles,
tags, case counts, 281-assertion double green), the ipc fix's race-freeness
(registration at `ipc_channel.cpp:198` happens-before the send at :211), the
case counts matching the gate logs, the accuracy of the #1335 attribution,
and every DECISIONS.md entry against the code. "No REQUIRE inside non-joined
threads, no sleeps, fixture cleanup RAII, singleton isolation holds,
meaningful before/after comparisons, no committed product-code changes, no
UB found."

### Findings and dispositions (all fixed in commit d668d274b + 0b48dc309)

| # | severity | finding | disposition |
|---|---|---|---|
| 1 | P0 | the `src/agent/CMakeLists.txt` link fix was present only as a dirty-worktree edit — the committed branch could not link the heavy lane | COMMITTED (d668d274b) with the full attribution comment |
| 2 | P1 | the link fix lacked `POSITION_INDEPENDENT_CODE ON` on `sicnu_agent_loop` (needed because `sicnu_agent` is SHARED; #1335 and the `sicnu_agent_ops` precedent both set it) | ADDED (d668d274b, `src/agent_loop/CMakeLists.txt`) |
| 3 | P1 | three REQUIREs between `std::thread` creation and join in the ipc case → a failed REQUIRE would unwind past a joinable thread → `std::terminate` kills the whole binary | RAII `JoinGuard` added; the two mid-case REQUIREs now can't skip the join (0b48dc309) |
| 4 | P2 | boundary TOCTOU case leaves a dangling `setContributionSink` pointer on a REQUIRE-abort path | RAII `SinkReset` added (0b48dc309) |
| 5 | P2 | unload-order fixture deleted the SHARED default snapshot root (`<tmp>/sicnu-plugin-snapshots`) although its own snapshots live under its private `tempDirectory` | extra deletion dropped; comment documents the ownership (0b48dc309) |
| 6 | P2 | ledger claimed "2,529 assertions total"; logs sum to 1,872; EVIDENCE said "six r4 suites" while listing five | corrected from the logs (1,873 per pass after the positive-control assertion); "five r4 suites" |
| 7 | P3 | halffail hardcoded the Linux entrypoint name (`libbroken_plugin.so`) | platform branches added (mirrors every sibling suite) |
| 8 | P3 | boundary case 3 passed the plugin ID as the entrypoint string | passes `kHelloEntrypoint` |
| 9 | P3 | unload-order case 4 title overstated the induced race | retitled "…(capture possibly in flight)…" with the race-neutrality documented in-file |
| 10 | P3 | `snapshotRootEntries()` emptiness can pass vacuously on a missing root | positive control: the ok-mode case now waits (bounded) for the dev-mode last-good capture and asserts the root non-empty BEFORE any failure case claims emptiness |
| 11 | P3 | parity temp dir leaked on REQUIRE-abort paths | RAII `TempDirCleanup` |

### Pass 1 → remediation verification

After the remediation commits the full WP-F gate was re-run in the fresh
heavy `build-dev/`: **20/20 suites, two consecutive passes, 1,873 assertions
per pass, zero failures** (`gate_pass1.txt` / `gate_pass2.txt`, re-recorded).
The halffail positive-control rework (finding 10) initially failed because
the fixture had not enabled `devMode` (successful loads only publish
last-good captures in dev mode) — fixed in the same commit and covered by the
re-run.

## Known residuals (documented, not silently absorbed)

- D-6: the host-process session folds the worker's typed
  `InitializationFailed` into `LibraryLoadFailed` — pinned as the observed
  contract; preserving the worker's typed code through the session report is
  a protocol/forwarding improvement left as a follow-up.
- D-2: #1334's legacy-host whitelist gates (world-writable dirs, symlink
  policy, IID pre-check) are not on master and live in `src/core` (outside
  this track's whitelist) — the boundary matrix pins the on-master
  containment seams instead; the whitelist tests belong to #1334's own
  `test_plugin_host_allowlist`.
- D-3: no load-time dependency-resolution gate exists on master; the invalid
  dependency SPEC refusal is pinned; resolution would be new functionality.
- The deliverable-count floors that assumed the P1-9 failures exist on Linux
  (they do not) are documented in EVIDENCE.md §1 and the PR body rather than
  padded with make-work.
