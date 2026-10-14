# R13-I Final Report (draft, updated as rounds complete)

## Scope delivered

| WP | Subject | Outcome | Commit |
|---|---|---|---|
| WP-01 | CI push concurrency per-SHA (#1490) | FIXED — group keyed on sha for push, per-ref+cancel for PR | 8d0cd2a2c7 |
| WP-02 | CI status honesty (#1491) | FIXED — collect_readiness cancelled/unknown fail-closed; new scripts/ci_master_status.py offline classifier | cb9fe08832 |
| WP-03 | clang-tidy gate (#1495) | FIXED — blocking + header-filter to repo src/ (Catch2 flooding proven on real CI log) | 8821d0f62c |
| WP-04 | orphan test census (#1494) | FIXED — m1_2 registered; scripts/check_test_registration.py gate | (pending build) |
| WP-05/06/07 | #1468 cluster | VERIFIED-NO-OP on master (all 5 items already fixed with in-code #1468 references); fixture tests run as GREEN evidence | — |
| WP-08 | presets + parallelism | presets ALL PASS ×2; build.sh selftest EACCES fixed; build_with_otb.sh + verification_ladder.py honor CMAKE_BUILD_PARALLEL_LEVEL | ee206fb7f3 |
| WP-09 | offline bundle manifest | VERIFIED — canonical verifier fail-closed on tamper/missing/unlisted/traversal/schema (5-case matrix + 18-check conformance) | — |
| WP-10 | Windows scripts | STATIC PASS (typed diagnostics, exit propagation, quoting, UTF8, setlocal-scoped PATH, UNC-safe); runtime NOT-EXECUTED (no host) | — |
| WP-11 | AppImage toolchain | VERIFIED — fetch_tool matrix pass/tamper/PENDING; stale "unit-tested" comment corrected | 5bcebc7d65 |
| WP-12 | install/DESTDIR | partially verified (config files path-clean; offline_smoke fail-closed); full install NOT-EXECUTED (no built artifacts at review time) | — |
| WP-13 | discovery stability | ctest -N byte-identical ×2, 992 tests; discovery design audited (PRE_TEST tolerant, per-case process, tier labels) | — |
| WP-14 | offline zero-leak | VERIFIED — dead-proxy + sys.audit netgate, zero egress; check_mcp_stdio missing-exe fail-closed fix | ee206fb7f3 |
| WP-15 | generator zero-drift | VERIFIED — gen_lab_docs byte-identical ×2, --check RED on perturbation; check_lab_registry ×2; OFFLINE_BUNDLE.md claims match scripts | — |
| WP-16 | perf baseline + tooling | perf_report.py KeyError crash fixed (RED→GREEN×2); local timings in PERFORMANCE.md | 5ba11e7b29 |

## Independent review

- Round 1 (agent-25, no implementation role): **BLOCK** — 1 P1 + 3 P2 + 8 P3, all with live repros. Every finding closed (P1-1 census wired as ctest lane; P2-2 .gitignore negation so evidence ships; P2-3 template anchors verified from live text; P2-4 dual-root header filter + full ERE escapes + fail-closed ref fallback; P3-5..P3-12 per ROUNDS.md Round 19).
- Round 2: **APPROVE** — all 12 closed; 4 new P3 nits (N1-N4) fixed same-day (8bae62280c).
- Review artifacts: .planning/r13-i-infra/REVIEW_INDEPENDENT.md (both rounds, with repro outputs).

## Cross-domain defects found but NOT fixed here (ownership rules)

1. test_io_vector_contract:352 failure on master (issue #1491's tail) — geospatial-io domain (R13-B side / dormant fix/all-open-issues claimed it). Evidence: run 38165329116 tier-1 log. Needs a domain-owner PR.
2. macOS Tier-3 build seam failure (run 38413453866, "Verify macOS Build") — platform build failure on master; needs a macOS host to diagnose. Not masked by anything in this track.
3. GitHub-side stuck run id 38415387705 (in_progress ~19h despite 360-min cap) — platform anomaly; cancelled runs dominate master history. Not repo-fixable.

## Host/verification limitations (honest)

- No Windows host and no pwsh: all .cmd/.ps1 verification is static.
- No cloud CI awaited or required; all evidence is local + historical CI logs.
- This host's Qt6 exports -mno-direct-extern-access (GCC-only) into compile_commands.json; clang-tidy rejects it. CI evidence (run 38430064386) shows the flag absent there.

## Commits (origin/master 892295685e36..HEAD)

11 commits: ci concurrency (#1490), clang-tidy gate (#1495), readiness/status classifier (#1491), parallel-cap + selftest + mcp fixes, perf_report KeyError, appimage comment truth, m1_2 registration + census (#1494), TEST_INFRA doc, review round-1 and round-2 closures. Full evidence in .planning/r13-i-infra/ (ROUNDS.md, evidence/*, REVIEW_INDEPENDENT.md).
