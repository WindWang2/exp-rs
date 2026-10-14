# R13-I Ownership

## My exclusive write scope (this branch)

- `.github/workflows/ci.yml` (concurrency block + clang-tidy step only)
- `scripts/collect_readiness.py`, `scripts/ci_master_status.py` (new), `scripts/check_test_registration.py` (new), `scripts/run_clang_tidy_changed.sh`
- `tests/CMakeLists.txt` — APPEND-MINIMAL only: register test_challenger_m1_2_harness; nothing else
- `.planning/r13-i-infra/**`
- Possibly small fixes in `scripts/check_presets.py`, `scripts/verify_bundle_manifest.py`, `scripts/offline_smoke.sh`, `scripts/perf_report.py`, `scripts/gen_lab_docs.py` IF a real defect is proven by a RED fixture first.

## Explicitly NOT mine (read-only)

- `src/**` (all production code — R12 A/B/C/D/E/F domains)
- `data/cartography/**` (R13-G)
- `tests/test_io_vector_contract.cpp` and other per-test sources outside tests/support (domain tracks; failing io test on master is a cross-domain defect → issue note, not my fix)
- `.goal-loop-ledger.md` (shared root ledger — not written to avoid parallel-write conflicts)

## Shared-hotspot protocol

- `tests/CMakeLists.txt`: checked against dormant branch `hardening/integration-build-contract-drift` (its +165-line registration block does NOT include challenger m1_2 → no overlap). Re-check `git diff origin/master...HEAD -- tests/CMakeLists.txt` of all active branches before PR.
- Root `CMakeLists.txt`: no change planned; only if a WP proves necessity.

## Scope notes added during execution
- TEST_INFRA.md:75 — one-line doc correction (lint lane non-blocking → blocking) required by WP-03; documentation of my own changed behavior, no code semantics.
- packaging/appimage_toolchain.sh — comment-only correction of a false "unit-tested" claim (WP-11).
