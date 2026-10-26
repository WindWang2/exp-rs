# R22-07 Ownership & Write-Scope Compliance

## Files written by this track (final set)

| Path | Issue | Whitelist basis |
|---|---|---|
| `CMakeLists.txt` (root) | #1698 | 根 CMakeLists 允许 |
| `src/cli/CMakeLists.txt` | #1698 | "安装目标小修" 特例 |
| `src/lab/session_state.cpp` | #1696 | `<algorithm>` include 特例（1 行 + 无其他改动） |
| `src/verification/availability.h` → `verification_availability.h` (+11 include/reference updates in `src/verification/*` + `tests/test_verifier_provenance_14.cpp`) | #1696 | "macOS 头文件重命名/引用替换" 特例 |
| `scripts/build_offline_bundle.sh` / `.cmd` | #1697 | scripts/** |
| `scripts/run_clang_tidy_changed.sh` | #1681/#1683 | scripts/** |
| `.github/workflows/ci.yml` | #1681/#1682/#1683 | .github/** |
| `.github/workflows/platform-tests-optin.yml` (new) | #1682 | .github/** |
| `tests/sicnu_test_time_budget.h` (new) | #1699 | 基础测试工具 |
| `tests/test_r22_infra_bundle_guard.sh` (new) | #1697 | tests/test_r22_infra_* |
| `tests/CMakeLists.txt` | registration | 跨轨注册表 append-only 尾块 |
| `tests/test_agent_ops_ui_smoke.cpp`, `test_execution_governor_11.cpp`, `test_data_manager.cpp`, `test_data_source_providers.cpp`, `test_asset_catalog_index.cpp`, `test_data_foundation_scale.cpp`, `test_execution_plane.cpp` | #1699 | 测试可信性 = 本轨核心任务域（issue 明确指派这些文件；改动仅限墙钟断言及其直接支撑，一处 lint 债 `performance-unnecessary-copy-initialization` 因该文件已进入本 diff 而顺手修复） |

## Explicitly NOT touched (out of whitelist; recorded instead)

- `src/teaching_admin/script_adapters.cpp` `bundleBuilderRequest` — C++ 侧对 `--version` 的前置校验（defense-in-depth，教学轨文件）→ CROSS_TRACK_FINDINGS.md #1。
- `src/dataset/dataset_ids.h:21`、`src/experiment/experiment_ids.h:14` — clang-tidy `bugprone-macro-parentheses` 旧债（本轨 lint lane 触发暴露；文件属数据/实验轨）→ CROSS_TRACK_FINDINGS.md #2。
- 仓库根一次性脚本（`build_r6.cmd` 等，#1683 项）→ 根目录文件不在白名单 → CROSS_TRACK_FINDINGS.md #3。
- actions 第三方 SHA pin（#1683 项）→ 需要 Dependabot 配套策略，本轨只钉住了 qca/qtkeychain 源码克隆 → CROSS_TRACK_FINDINGS.md #4。

## Process rules honored

- Single worktree/branch; no master writes; no stash use; `git add` only explicit paths.
- All builds under `/tmp/exp-rs-r21-build.lock`, `-j4` hard cap (CMAKE_BUILD_PARALLEL_LEVEL=4).
- No deletion/weakening of tests; no golden/tolerance changes; no `git push --force`; no CI wait/fix as completion gate.
