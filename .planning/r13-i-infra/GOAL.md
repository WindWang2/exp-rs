# R13-I Goal & Oracles

GOAL: Converge build/CTest/CI/offline-packaging reliability for existing functionality only.
No new product surface; no src/** production changes; no second engines.

## Per-WP oracles (falsifiable)

- WP-01 (#1490): two push events to master produce DISTINCT concurrency groups (static matrix proof over the workflow expression); PR events keep one group per ref with cancel-in-progress=true. Yaml must parse.
- WP-02 (#1491): collect_readiness.py counts cancelled/unknown ladder statuses fail-closed (never as pass, counted as attention); new offline CI-run classifier maps {success, failed-test, failed-build, failed-configure, infra, cancelled, timeout, pending} fixtures to correct buckets. RED: fixture demonstrating dropped/uncounted status; GREEN×2.
- WP-03 (#1495): with a deliberately buggy changed C++ file, run_clang_tidy_changed.sh exits nonzero; with clean diff exits 0; CI step no longer continue-on-error. Legacy debt not flooded (changed-scope preserved).
- WP-04 (#1494): test_challenger_m1_2_harness compiles, is discovered by ctest -N, and runs; census script scripts/check_test_registration.py exits nonzero on any unexplained orphan test_*.cpp and clean on master state. GREEN×2.
- WP-05/06/07 (#1468): verified-no-op on master; run mini_cog/http_range fixture tests (test_io_identity / test_io_microbench / mini_cog consumers) GREEN on build-r13-i; netcdf guard proven by configure fixture.
- WP-08: scripts/check_presets.py (or fixed successor) passes on CMakePresets.json; no build script can raise parallelism above 2 from env (static grep proof).
- WP-09/12: verify_bundle_manifest.py detects tamper/missing/extra on a synthetic bundle; cmake --install into temp prefix works and is removable; offline_smoke.sh behavior honest when tools missing.
- WP-14: offline_smoke / MCP stdio check produce no-network evidence on loopback-only env; logs contain no tokens.
- WP-15: gen_lab_docs.py / check_presets.py / check_lab_registry.py byte-identical on rerun; drift => nonzero exit.
- WP-16: perf_report.py baseline recorded (build wall time, ctest elapsed, RSS where measurable) in PERFORMANCE.md.
- WP-10/11: static/mock only on this Linux host; explicitly reported as not-executed where true.

## Exit gate

RED/GREEN×2 per real change; `git diff --check` clean; independent review subagent (not involved in implementation) over `git diff origin/master...HEAD`; single PR with evidence matrix. No waiting on cloud CI.
