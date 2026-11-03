# R27-T05 Oracles（每条 AC 一个可失败命令）

统一在 worktree `build-r27-t05` 内执行，`QT_QPA_PLATFORM=offscreen`，关键项双跑。

| AC | 命令 | 期望 |
|---|---|---|
| tests/CMakeLists 可解析（P0） | `cmake -S . -B build-r27-t05 -DENABLE_TESTS=ON` exit 0 | configure 成功 |
| #1744 判分 | `ctest -R "test_r27_t05_grader_window::"` | 全过（band 外永不满分、loader 拒错侧窗口、级联 verdict） |
| #1744 兼容 | `ctest -R "test_grader_engine::\|test_grader_schema::\|test_r16_a_grader"` | 全过（钉死 mid-window 值不变） |
| #1773 | `test_r27_t05_grader_window "*required stage*"` | missing→Partial、有→Pass |
| #1777 | `test_r27_t05_grader_window`（后续补 answer oracle）+ test_grader_engine 回归 | 全角折叠/否定/冲突归一 |
| #1775/#1776/#1780 | `ctest -R "test_r27_t05_workflow_placeholders::"` | 全过 |
| #1775/#1776 回归 | `ctest -R "test_placeholder_grammar"` | 全过 |
| #1768/#1781/#1782/#1785/#1786/#1787 | `ctest -R "test_r27_t05_experiment_integrity::"` | 全过 |
| #1772 | `ctest -R "test_r25_b_replay_deviation_v2\|test_experiment_evaluation"` | 过（env 诊断化后语义一致） |
| #1774/#1778 | `ctest -R "test_repair_planner_completion\|test_workflow_repair"` | 过（bounds 不再 -7；refusal-last 保序） |
| #1770 | lineage depth 上限置 budgetExhausted（由既有 lineage 测试覆盖回归） | 过 |
| #1771 | `ctest -R "checkpoint"` 相关 | 过 |
| #1735 编译 | `cmake --build build-r27-t05 --target test_r21_e_studio_verifier_hook` | 编译通过 |
| #1736 | `ctest -R "test_adversarial_m6"` | 过（真实 #784 场景） |
| #1737 | `ctest -R "test_contract_projection_9\|test_contract_platform_9\|test_help_coverage\|test_command_contract_9\|test_capability_surface_parity\|test_r22_data_security_consistency"` | 逐个修复后全过 |
| #1783/#1784 | bundle import 拒硬链接/换文件（extend test_r27_t05_experiment_integrity 或既有 bundle 测试回归） | 过 |
| #1748 | `packaging/appimage_toolchain.sh` 校验真实下载产物 | sha256 匹配 |
| #1749 | `scripts/build_offline_bundle.sh` assemble 后 bundle/lib 有 libsicnu_*.so（install-tree 路径） | lib/ 存在 |
| #1750 | `ctest -R "test_teaching_admin_script_adapters_r8\|test_paths"` | SKIP 计数可见 |
| #1732 | `gh api repos/WindWang2/exp-rs/rulesets` 审计记录 + RULESET_PROPOSAL.json | 审计存档（不实施） |
| 白名单 | `git diff --name-only $(git merge-base origin/master HEAD)..HEAD` | 全部落在白名单+tests/.planning |

## RED 记录（修复前失败证明）

- #1744：修复前 AtLeast gap 值 0.75 得 ~100 分（r18e 旧断言 `rawEarned > 99.9` 钉死了该 bug 行为——即 RED 证据由旧钉死断言本身构成）；修复后同值 40 分。
- #1773：修复前 missing-stage verdict=Pass（可从旧 verdict 分支推导：requiredStageOutcomes 不参与判定）；修复后 Partial。
- #1768/#1781/#1782/#1785/#1786/#1787/#1775/#1776：oracle 测试文件在未修复 HEAD 上必然失败（对照 git stash 生产 hunk 重跑验证，见 TEST_RESULTS.md）。
