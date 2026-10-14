# R13-I Round Ledger (chronological, command-grade evidence)

## Round 1 — recon (2026-10-14 ~13:50 CST)
- `git fetch --all --prune`; BASE_SHA=892295685e3606f3ac54c959954dde626508cc17.
- 0 open PRs; 30 open issues; merged #1483/#1484/#1496/#1497 + #1482 confirmed.
- `gh run list --branch master`: 25 latest = 15 cancelled / 2 pending / 3 failed-test / 1 failed-build(+test) / 4 older failures; 0 success. #1491 re-verified LIVE.
- Dormant-branch audit: fix/all-open-issues (local-only megabranch, src/** mass edits — avoided, D2); integration-build-contract-drift (tests/CMakeLists +165, no m1_2 — no overlap); R13-G worktree at base (no commits).

## Round 2 — WP-01 (#1490) commit 8d0cd2a2c7
- Fix: ci.yml concurrency group → `...-${{ github.event_name == 'push' && github.sha || github.ref }}`.
- GREEN×2: python yaml parse + expression matrix proof (2 consecutive runs PASS): push shaA≠shaB groups; PR same-ref group shared; cancel-in-progress stays PR-only. `git diff --check` clean.

## Round 3 — WP-03 (#1495) commit 8821d0f62c
- Fix: drop `continue-on-error` from clang-tidy step; run_clang_tidy_changed.sh gains `--header-filter='^<repo>/src/'`.
- Key finding: `.clang-tidy` HeaderFilterRegex 'src/.*' matched `build*/_deps/catch2-src/src/...` → blocking gate would have failed every test-touching PR on Catch2 macro debt. Proven locally AND on real CI log (PR run 38430064386: 14 errors all from catch2 headers).
- RED: injected use-after-move into tests/test_flood_fill.cpp → exit 123 with exactly the injected error (post-fix). Pre-fix run showed 9 extra catch2-header errors (the flooding defect).
- GREEN×2: clean diff → exit 0 ×2; clean comment-only C++ change → lints, exit 0.
- Host note: openSUSE Qt6 exports GCC-only `-mno-direct-extern-access` into compile_commands.json; clang-tidy 23 errors on it (unknown argument). CI run logs show the flag absent there. Local verification used a filtered copy of build-r13-i/compile_commands.json (sed removal); CI unaffected.

## Round 4 — WP-02 (#1491) commit cb9fe08832
- collect_readiness.py: cancelled/unknown statuses now counted in own buckets, attention-level; bucket conservation (sum == capability count) asserted by matrix.
- RED: cancelled fixture → sum(counts)=45/46, verdict invisible. GREEN×2: 6-case matrix (pass→ready; cancelled/failed/timeout/unknown→attention; skipped→insufficient-evidence), two consecutive full passes.
- scripts/ci_master_status.py (new): offline classification of saved gh run dumps; stage classification configure/build/test/infra with per-job detail; --gate fails on cancel waves / latest-failure / timeout-only.
- GREEN×2 fixture matrix: 8-run mix exact counts; gate exits 0/1/1/1 across the 4 scenarios, two consecutive rounds.
- Real data artifact committed: .planning/r13-i-infra/evidence/master-runs-2026-10-14.json + master-status-2026-10-14.json → latest completed = failed-test (Tier-1 test_io_vector_contract) + Tier-3 macOS failed-build; gate FAIL ⇒ #1491 still live.

## Round 5 — WP-08/WP-15 (implementation subagent A, evidence wp08-15.md)
- check_presets.py ALL PASS ×2 (37 assertions) + `cmake --list-presets=all` resolves 16 presets.
- build.sh selftest RED: EACCES re-exec (file is 100644) → fix via `$BASH` re-exec → GREEN×2.
- Parallelism audit: found build_with_otb.sh `make -j$(nproc)` and verification_ladder.py default min(8,cpus) ignoring CMAKE_BUILD_PARALLEL_LEVEL.
- WP-15: gen_lab_docs.py byte-identical ×2 + --check exit 1 on stale/untracked perturbations (/tmp copies); check_lab_registry.py GREEN×2 + RED exit 1; OFFLINE_BUNDLE.md claims statically verified vs scripts. No new gate needed (generators already have --check convention).

## Round 6 — WP-08 tail (lead) commit ee206fb7f3
- build_with_otb.sh: `make -j"${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"`.
- verification_ladder.py: `_default_build_jobs()` honors env pin, rejects non-integer fail-closed.
- GREEN×2: expansion proofs (pinned→-j2, unpinned→unchanged default, garbage→SystemExit) two rounds; bash -n OK.

## Round 7 — WP-09/WP-12/WP-14 (implementation subagent B, evidence wp09-12-14.md)
- WP-09: synthetic bundle matrix — valid PASS; 1-byte tamper exit 1; missing exit 1; unlisted exit 1; traversal (/.., C:/, /etc) rejected exit 1; schema /3 and min_reader_schema>2 refuse exit 2; escaping symlink flagged. Conformance suite 18/18. VERIFY.sh delegates to canonical verifier; PS twins documented mirror. NO DEFECT → verified-no-op.
- WP-12: install into temp prefix NOT-EXECUTED (build-r13-i has no built artifacts yet — honestly recorded); installed cmake package configs carry no build-tree absolute paths; offline_smoke.sh fail-closed without CLI.
- WP-14: zero-egress proven via dead-loopback proxies + sys.audit netgate abort-on-socket for verify_bundle_manifest.py and check_mcp_stdio.py (mock JSON-RPC server PASS; wrong-server-name negative FAIL). check_mcp_stdio.py missing-exe traceback → fixed fail-closed exit 1 (GREEN×2). No secret echoing found.

## Pending rounds
- WP-04: registration added; build+run awaiting global build lock (R13-G/H ahead in queue).
- WP-05/06/07: #1468 verified-no-op on master; fixture tests (test_remote_source_cache, test_execution_benchmarks, test_io_identity/microbench) to run after build.
- WP-10/11: static verification next.
- WP-13/WP-16: after builds.

## Round 8 — WP-11 (lead) commit 5bcebc7d65
- appimage_toolchain.sh fetch_tool matrix (no network, /tmp fixtures, APPIMAGE_TOOL_CHECKSUMS override): good-hash pass ×2, tampered exit 1, PENDING exit 1, post-edit re-run GREEN×2. bash -n OK on both packaging scripts.
- Fixed stale comment claiming nonexistent unit tests (grep proof: no test references fetch_tool).
- Checksums file state: both linuxdeploy entries PENDING → AppImage build is fail-closed by design (honest non-pass). Full AppImage link/smoke NOT-EXECUTED (needs app binary + tools download).

## Round 9 — WP-10 (lead, static; evidence wp10-11.md)
- _env.cmd / setup.cmd / build.cmd / {check_mcp,grade_all,run_lab}.cmd / bundle_manifest.ps1 / check_mcp_stdio.ps1 / seed_toolchain_cache.ps1 / bundle_dependency_report.ps1 statically reviewed against the WP-10 checklist: typed missing-tool diagnostics ✓, exit propagation ✓, quoting space-safe ✓, UTF8 outputs ✓ (ps1 no-BOM writes, UTF8 stdio decode), PATH mutations all under setlocal ✓, UNC-safe (no cd) ✓. No defect in scope. Windows runtime NOT-EXECUTED (no host, no pwsh).

## Round 10 — WP-16 partial (lead) commit 5ba11e7b29
- perf_report.py diff crashed KeyError on records lacking peak_rss_mb (RED, exit 1 traceback). Fixed: missing metric prints "(no data)"; wall regression logic guarded. GREEN×2: fixture diff twice (wall +40% regression still flagged, rssd "(no data)"); RSS-bearing fixture shows real delta.
- Build timing so far: configure build-r13-i 54s (cold) / 33.6s (re-run); ctest -N 992 tests deterministic ×2.

## Round 11 — WP-13 static (lead)
- ctest -N on build-r13-i: byte-identical across two runs (cmp), 992 tests, includes test_challenger_m1_2_harness (Test #971) post-registration.
- Discovery design audit: PRE_TEST fault-tolerant discovery (SicnuCatchAddTests.cmake), per-case process isolation (#1414), FAST/SLOW/LONG_RUN tier labels with FAST default, RUN_SERIAL honored. Challenger family (m1_2/m2_1/m2_2/m3_1/m3_2) uses raw add_test without labels by family convention (own-main harnesses); m1_2 follows the family + gets TIMEOUT 300 (its siblings have none — deliberate bound, recorded in DECISIONS).
- TEST_INFRA.md:75 said lint lane "non-blocking" — stale after WP-03; corrected (scope note added to OWNERSHIP.md).

## Round 12 — conflict re-check (lead)
- `git fetch origin master`: still 8922956 (unchanged since baseline).
- R13-G worktree: no tests/CMakeLists.txt delta. R13-H worktree: edits three test_*.cpp in its domain, no tests/CMakeLists.txt delta. No overlap with my registration hunk.

## Round 13 — WP-04 build/run (lead) commit 9420c559c9
- Queue: global lock held by R13-G (14:01-15:30, QGIS-gui chain) then R13-H; my build queued 14:17, acquired ~15:33. Lock contention itself is evidence the governance is needed and working.
- Build: 25 steps, exit 0 (sicnu_platform + sicnu_runtime + harness; runtime is Qt/QGIS-free by construction).
- Harness pre-check during queue wait: syntax-only compile with the real compile_commands flags → exit 0.
- ctest -R '^test_challenger_m1_2_harness$': PASS 0.31s, PASS 0.35s (two consecutive). Census --build-dir: 0 orphans exit 0 (third GREEN).
- Decision: harness completes in ~0.3s → stays unlabeled (family convention, FAST default honest). TIMEOUT 300 kept as the deliberate bound.

## Round 14 — WP-05/06/07 fixture build launched (lead)
- Targets: test_remote_source_cache + test_execution_benchmarks (mini_cog_server.h consumers), test_io_identity + test_io_microbench (http_range_server.cpp consumers) — the fixtures named by #1468.
- WP-05 static re-verified: sicnu_test_main.cpp returns normally; qt_lifecycle.h:20 "Do not call std::_Exit anywhere" + testRunEnded ordered teardown confirmed present.

## Round 15 — WP-05/06/07 fixture GREEN (lead)
- Build: 3595 steps under global lock, -j2, exit 0 (includes qgis_core/gui chain needed by the io fixtures).
- Fixture runs (direct binaries, QT_QPA_PLATFORM=offscreen), two consecutive each:
  - test_remote_source_cache (mini_cog): 6 cases / 34 assertions PASS ×2
  - test_execution_benchmarks (mini_cog): 15 cases / 256 assertions PASS ×2
  - test_io_identity (http_range_server): 8 cases / 93 assertions PASS ×2
  - test_io_microbench (http_range_server, byte/requests/peak gates): 2 cases / 23 assertions PASS ×2
- All four processes terminate normally → fixture server dtors bounded (the #1468 hang shape would have hit TIMEOUT/ctest hang; none observed).
- WP-07 netcdf guard: cmake -P probe of the guard pattern with forced-NOTFOUND → exit 1 with the actionable message (twice); positive branch proven by the successful full configure of build-r13-i (netcdf present on host; io targets linked).
- Verdict: #1468 items are verified-no-op on master (code carries the fixes with in-line #1468 references); no new diff manufactured.

## Round 16 — WP-03 header-path RED (lead)
- Injected bugprone-use-after-move into src/analysis/classification/rs_flood_fill.h + comment-trigger in tests/test_flood_fill.cpp: lane exit 123, error reported AT THE HEADER with the narrowed filter → first-party header findings still gate. Probes reverted; lane GREEN ×2 after revert.

## Round 17 — WP-12 install/DESTDIR (lead)
- `cmake --install build-r13-i --prefix /tmp/r13i-prefix` (temp prefix, non-root): installed 1029 files (headers + built libs); .so exec bit 755 preserved; libqgis_core.so symlink preserved; RPATH rewritten to `$ORIGIN:$ORIGIN/../lib` (relocatable); no absolute build paths in installed text configs (grep).
- Re-run: per-file "Up-to-date" (idempotent); missing target (libsicnu_core.so, unbuilt in this tree) aborts LOUDLY via "file INSTALL cannot find" (fail-closed, no silent partial success). Note: first attempt piped through tail masked the exit code — measurement trap recorded; direct re-run shows the honest exit 1.
- Full CLI install NOT-EXECUTED: blocked by cross-domain #1499 (master compile break in src/python/isolated). Prefix removal leaves nothing behind.
- cmake --install has no transactional rollback (documented limitation); bundle-level integrity is covered by the manifest verifier (WP-09).

## Round 18 — cross-domain P0 found & filed as issue #1499 (lead)
- During the WP-12 CLI build: master HEAD's src/python/isolated/python_worker_process_pool.cpp FAILS to compile (85/86 brace imbalance; namespace closed at line 177 by an orphan `}` at :170 left by merge 87de7c8692's conflict resolution; acquireWorker's deadline/processEvents/sleep block unreachable → would hang with no free worker).
- A/B proof: master file errors at :179; same file with only line 170 removed compiles clean with identical flags.
- Not fixed here (src/** ownership). Filed: https://github.com/WindWang2/exp-rs/issues/1499 with repro + minimal fix. This is the live demonstration of why WP-01 matters: master HEAD was never CI-built after the R12 merge wave.

## Round 19 — independent review round 1 → fixes (reviewer: agent-25, no implementation role)
- Verdict BLOCK: 1 P1 + 3 P2 + 8 P3. All P1/P2 closed; P3-5..P3-12 fixed where cheap:
  - P1-1: census wired as ctest lane test_registration_census (build-pinned Python, TIMEOUT 60); TEST_INFRA.md documents it. PASS x3; neighbor parity lane still PASS.
  - P2-2: .gitignore gains !.planning/r13-i-infra/ (single-negation convention); ledger+evidence now committable.
  - P2-3: template_registrations() expands the SICNU_VERIFIER_LANES family from LIVE text; anchor-missing/lane-dropped/comment-masked RED fixtures all exit 1.
  - P2-4: tidy header filter anchors logical+physical root, full ERE escape class; header RED re-proven (exit 123), clean GREEN x2.
  - P3-5 comment stripping (census), P3-6 lazy ladder default + negative rejection + pin-disagreement note, P3-7 shutil.which fallback, P3-8 timeout class + word-boundary tokens + datetime sort, P3-9 integer validation, P3-10 unresolvable-refs exit 2, P3-11(b) comment cites the committed 25-run dump, P3-12 disappeared-metric signal.
- Commits: 58571bbe9b (census), da7bf1cd52 (review round-1 hardening).

## Round 20 — review round 2 (agent-25 resumed) + nit closure
- Round-2 verdict: APPROVE. All 12 round-1 findings CLOSED with live repros.
- New nits N1-N4 (all P3): N1 \btest\b broke the plural "tests" (fixed: tests?|execut\w*), N2 stale PATTERN_ALLOWLIST comment in tests/CMakeLists.txt (fixed), N3 multi-line #[[ ]] masking (fixed with a bracket state machine; trailing-bracket limitation documented), N4 "00" pin (rejected). Commit 8bae62280c.
- Re-runs: census real tree exit 0 x2 + ctest lane PASS; ci_master_status gate matrix 0/1/1/1 x2 unchanged; "Run unit tests" -> failed-test; "Attest provenance" -> failed-unknown.

## Closing state
- All 16 WPs addressed: 5 fixed-with-diff (WP-01/02/03/04 + WP-08 parallel-cap tail), WP-05/06/07 verified-no-op (#1468 already fixed on master, fixtures GREEN x2), WP-09/11/14/15 verified with fixture matrices, WP-10 static-only (no Windows host), WP-12 partial (full install blocked by cross-domain #1499), WP-13 discovery determinism + census lane, WP-16 perf_report fix + timing baseline.
- Cross-domain: issue #1499 filed (master compile break, src/python) with minimal-fix A/B proof.
- Independent review: 2 rounds, BLOCK -> APPROVE, all findings closed with repros.
