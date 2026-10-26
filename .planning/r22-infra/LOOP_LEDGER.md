# R22-07 Loop Ledger (goal-loop protocol, one row per verified iteration)

| Round | Hypothesis / change | Files | Verification command | Real result | Next |
|---|---|---|---|---|---|
| 0 | Inventory: master/PRs/issues live | — | git/gh commands (BASELINE.md) | 7/7 issues OPEN_UNFIXED; R21 PRs file-map captured; no scope collisions except 2 manageable | create worktree |
| 1 | #1696 fix: `<algorithm>` + header rename | session_state.cpp; verification/* (12 refs) | grep completeness; Release build of lab+verification targets (in progress at row time) | old-name refs = 0; build completed OK later (TEST_RESULTS.md) | #1697 |
| 2 | #1697 `.sh` guard v1 (post-parse validation + containment) | build_offline_bundle.sh | `sh tests/test_r22_infra_bundle_guard.sh --repo-root .` | GREEN all legs | RED proof |
| 3 | RED proof attempt vs master script | (sandbox only) | master script + `--version '1.0/../../victim'` | **Canary SURVIVED** — traversal needs existing prefix (`rm -f` swallows ENOENT; `mkdir -p` still jams outside `--out`) | rerun with pre-existing prefix |
| 4 | RED proof with pre-existing `out/sicnu-lab-1.0` | (sandbox) | as above | **CANARY_DELETED_BY_OLD_SCRIPT** → issue confirmed & repro realistic (issue's own two-step repro) | .cmd twin |
| 5 | #1697 `.cmd` guard (`:check_version` PS probe, errorlevel propagation) | build_offline_bundle.cmd | structure review + pwsh regex battery | pwsh absent locally → regex identical to `.sh`; batch reviewed; NOT_EXECUTED (Windows) recorded | move both validations to parse-time for smoke-testability; re-run `.sh` suite GREEN |
| 6 | #1698 RPATH + worker install | CMakeLists.txt; src/cli/CMakeLists.txt | configure regen; install smoke queued post-build | install rules generated (cmake install output pending build) | #1681 |
| 7 | #1681 lint after build + #1683 merge-base | ci.yml; run_clang_tidy_changed.sh | yaml parse; local lane run | lane lists own files only via merge-base ✓ | real tidy pass |
| 8 | Full tidy pass on changed files | — | clang-tidy per file | found: my test copy-init (fixed), 2 out-of-scope header debts (→ CROSS_TRACK), host GCC-flag noise + nondeterministic SIGSEGV (env) | narrow header filter |
| 9 | Header filter → changed-headers only | run_clang_tidy_changed.sh | re-run lane | filter built from changed `.h` set; unchanged-header debt no longer blocks; bash -n OK | #1699 |
| 10 | #1699 nine sites (structural / hang-guard / scaled budget) | 7 test files + new header | compile + run affected cases (queued on Release build) | source complete; runs recorded in TEST_RESULTS.md | #1683 remaining |
| 11 | #1683: permissions, SHA pins (qca df5171e3, qtkeychain e63da286), CI fetch depth | ci.yml | yaml parse; gh api SHA resolution | OK | register tests |
| 12 | Register bundle-guard ctest + opt-in workflow | tests/CMakeLists.txt; platform-tests-optin.yml | yaml parse; script exec | OK | build → run oracle suite ×2 |
| 13 | Build attempt 1 (Debug+ccache) | — | ninja build | GCC ICE (stack-raise launcher skipped due to ccache launcher) → drop ccache | retry |
| 14 | Build attempt 2 (Debug+launcher) | — | ninja build | dwarf2out ICEs persist (deterministic, multiple TUs) → host toolchain regression | Release configure |
| 15 | Build attempt 3 (Release) | — | ninja build (background, lock, -j4) | see TEST_RESULTS.md | install smoke + tests ×2, dual review |
| 16 | Release build ×4 targets GREEN → install-target set build GREEN → full install RC=0 | CMake/tests | readelf/ldd/env -i/patchelf | RUNPATH $ORIGIN/../lib on cli+worker+libs; env -i --help RC=0 ×2; env -u LDP ldd resolves project libs via bin/../lib; patchelf --remove-rpath → loader failure 127 (RED) | #1699 test builds |
| 17 | 7 suites + verifier suite GREEN; ctest registration verified end-to-end | tests | direct runs + ctest -R | all PASS (assertion counts in TEST_RESULTS.md); ctest #1155 Passed 0.12s | R1 |
| 18 | R1 REJECT → 10 findings fixed (vacuous smoke, CI smokes env-deps, UNIX+sh registration, ccache key, symlink leg, bounded strtod, comments, same-mode containment) | ci.yml, .sh/.cmd, tests ×3 | bash -n/sh -n/yaml parse; guard ×2; header hostile-env; 3 suites re-run | all re-verified GREEN | R2 |
