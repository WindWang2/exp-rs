# R13-I Baseline

- BASE_SHA: `892295685e3606f3ac54c959954dde626508cc17` (origin/master, fetched 2026-10-14 CST)
- Worktree: `/home/kevin/projects/rs-studio/exp-rs-r13-i-build-infra`
- Branch: `hardening/r13-i-build-test-packaging` (upstream unset deliberately — never push to master)
- Build dir: `build-r13-i` (Ninja, Debug, ENABLE_TESTS=ON, ccache)
- Host: Linux, GCC (/usr/bin/c++), 16 cores, 62GB RAM, 793GB free disk
- Remote state at recon: 0 open PRs, 30 open issues; latest merged #1483/#1484/#1496/#1497 (R12 A/C/F/E) + #1482 (R11-D).
- CI state re-verified 2026-10-14: last 25 master runs = 0 success (many cancelled, 2 failure, 1 stuck in_progress ~19h id=38415387705, latest 89229568 pending id=38515287149). #1491 NOT stale.
- Parallel tracks detected: R13-G worktree `exp-rs-r13-g-cartography` at base, no commits yet (scope data/cartography — disjoint from mine). No R13-H worktree found.

## Issue disposition at baseline (verified on 8922956)

| issue | claim | verified-on-HEAD state | WP | disposition |
|---|---|---|---|---|
| #1490 | concurrency group cancels queued master pushes | TRUE — ci.yml:9-14 group=workflow-ref; run list shows mass cancellations | WP-01 | FIX |
| #1491 | master no completed CI since 2026-10-11 | TRUE (re-verified, see above); failing test_io_vector_contract:352 belongs to io domain (not my write scope) | WP-02 | FIX (reporting/classification side) + cross-domain note |
| #1494 | test_challenger_m1_2_harness.cpp unregistered | TRUE — absent from tests/CMakeLists.txt | WP-04 | FIX |
| #1495 | clang-tidy continue-on-error | TRUE — ci.yml:103-107 | WP-03 | FIX |
| #1468 P1 | sicnu_test_main.cpp _exit on Windows | ALREADY FIXED on master (returns normally, comment cites #1468) | WP-05 | verified-no-op |
| #1468 P2 | mini_cog_server recv no timeout | ALREADY FIXED (SO_RCVTIMEO 30s before serveConnection) | WP-06 | verified-no-op |
| #1468 P2 | http_range_server respond() ignores close / over-bills | ALREADY FIXED (uses caller close arg, bills effective bytes) | WP-06 | verified-no-op |
| #1468 P2 | unguarded NETCDF find_library | ALREADY FIXED (FATAL_ERROR at configure, tests/CMakeLists.txt:423-435) | WP-07 | verified-no-op |
| #1468 P2 | duplicate test_io_fabric_catalog link in WIN32 block | ALREADY FIXED (single registration at :615-621) | WP-07 | verified-no-op |

## Dormant-branch conflict audit (avoid preemption)

- `fix/all-open-issues` (worktree exp-rs-fix-all-issues, local-only, dormant since 2026-10-14 01:20): contains commits addressing #1490/#1491/#1494/#1495 among 20+ commits that also rewrite large parts of src/** — outside this round's per-track ownership model, never pushed, no PR. Decision: DO NOT build on it; implement independent minimal in-scope fixes. See DECISIONS.md.
- `hardening/integration-build-contract-drift` (dormant, upstream gone): edits tests/CMakeLists.txt (+165) registering 19 tests; does NOT register challenger m1_2. My tests/CMakeLists.txt edits must be append-minimal and re-checked against master at PR time.
- `fix/view-link-exit-retirement` (dormant): test_view_link std::_Exit teardown (#1395) — adjacent to #1468 P1 but a different test file; no overlap with my edits.
