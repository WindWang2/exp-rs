# R22-07 (r22-infra) Final Report

## Delivered

| Issue | Fix | Local evidence |
|---|---|---|
| #1696 P1 | `#include <algorithm>` in `src/lab/session_state.cpp`; `src/verification/availability.h` → `verification_availability.h` (+12 references, case-collision with macOS SDK `<Availability.h>` removed) | Release build of lab/verification/CLI/worker targets BUILD_EXIT=0; rename grep-clean full-tree (re-verified independently by R2); macOS/MSVC compile = CI (not waited on, honestly labeled) |
| #1697 P1 | `--version` whitelist (alnum-first, `[A-Za-z0-9._-]`) at assemble time + git-describe fallback revalidation + same-mode realpath containment before any deletion, in `.sh`; identical PowerShell-probe guard in `.cmd` (`:check_version`, fail-closed, injection-proof) | RED executed vs master (canary deleted with pre-existing prefix); GREEN ×2 + post-review ×2 + UTF-8-locale run; 10-input battery + legal/space/symlink/incremental legs; ctest-registered (`test_r22_infra_bundle_guard`, passed 0.12s) |
| #1698 P1 | `CMAKE_INSTALL_RPATH` `$ORIGIN/../lib;$ORIGIN` (Linux) / `@loader_path` pair (macOS), before any add_subdirectory; `install(TARGETS sicnu_worker …)`; CI install smokes strengthened (worker presence; explicit external-dep env on all three platforms; Windows `X_VCPKG_APPLOCAL_DEPS_INSTALL=ON`) | `cmake --install` RC=0; RUNPATH on cli+worker+libs via readelf; `env -i … --help` RC=0 ×2; `env -u LD_LIBRARY_PATH ldd` resolves project libs via `bin/../lib`; patchelf `--remove-rpath` → loader failure 127 (RED) |
| #1681 P1 | lint step moved after build (generated `ui_*.h`/`*.pb.h` exist when tidy runs); lane base → merge-base with loud shallow fallback; CI fetch depth 500 | lane executed locally lists exactly own files via merge-base; yaml validated |
| #1682 P2 | macOS/Windows PR-lane smokes (guard test / install smoke); opt-in `workflow_dispatch` macOS test workflow (reuses tier3 ccache pool) | yaml validated; CI execution not waited on per track rules |
| #1699 P2 | 9 assertion sites: structural split (agent_ops 500ms-worker/200ms-ceiling), hang-guards w/ kept structural oracles (governor 500ms; remote 10s), scaled budgets via new `tests/sicnu_test_time_budget.h` (Release×1.0 unchanged, ASan×4, bounded env override) | 7 suites + verifier suite all PASS (counts in TEST_RESULTS.md); header behavior executed in 3 modes + hostile values rejected |
| #1683 P3 | top-level `permissions: contents: read`; qca/qtkeychain clone pins (SHAs API-resolved, independently re-verified by R2); lane header-filter narrowed to changed headers (documented trade-off) | yaml validated; pins verified twice |

## Review record

- R1 (independent subagent): **REJECT** → 10 actionable findings fixed + re-verified (REVIEW_R1.md).
- R2 (second independent subagent, R1-blind): **APPROVE**, 0 P0/P1; 5 cheap hardenings applied from its P2/P3 list (REVIEW_R2.md).

## NOT_EXECUTED / honest limits

- Windows `.cmd` guard on real cmd.exe (no Windows host): structure + regex statically verified; CI Windows smoke pins exit 2 + refusal message.
- macOS/MSVC compiles, all CI lanes: not waited on (track rule); expected watch item — macOS brew @rpath linkage in the install smoke fails loudly if brew kegs need it (mitigation documented).
- Local clang-tidy full pass: host tool (GCC-only flag rejection + nondeterministic SIGSEGV) — advisory only; lane logic verified.

## Deferred (out of whitelist / needs RED evidence — see CROSS_TRACK_FINDINGS.md)

teaching-side `--version` pre-validation; dataset/experiment macro-parentheses debt; root legacy scripts; actions SHA pinning + Dependabot; Windows compiled-test opt-in lane (needs reusable deps workflow); G2/G4/G5 milestones beyond this PR.

## Branch hygiene

Single branch `agent/r22-infra-20261025T133318Z` off `origin/master@911e0b17`; one PR; no master writes; no force push; CI not waited on; worktree retained for revisions.
