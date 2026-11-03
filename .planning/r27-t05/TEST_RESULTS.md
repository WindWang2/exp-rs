# R27-T05 Test Results

环境：CachyOS x86_64 / 16C62G / GCC（CachyOS 编译器报告 ICE 一次于 -j2 大目标批，-j1 重跑零错误——非本轨代码问题）；构建 `build-r27-t05`（Release, ENABLE_TESTS=ON），全程 flock `/tmp/exp-rs-host-build.lock`，≤-j2；`QT_QPA_PLATFORM=offscreen`。

## P0：测试通道恢复

| 验证 | 命令 | 结果 |
|---|---|---|
| tests/CMakeLists 解析 | `cmake -S . -B build-r27-t05 -DENABLE_TESTS=ON` | exit 0（BASE 上 parse error，已重建；31 兄弟 target 零丢失，括号平衡=0） |
| #1735 编译解锁 | `cmake --build … --target test_r21_e_studio_verifier_hook` | 编译通过，14 断言全过 |

## Oracle 套件（新）×2 次运行，结果逐字节一致

| 套件 | 断言/用例 | Run1 | Run2 |
|---|---|---|---|
| test_r27_t05_grader_window（#1744/#1773/#1777） | 81 / 7 | 全过 | 全过 |
| test_r27_t05_workflow_placeholders（#1775/#1776/#1780） | 25 / 3 | 全过 | 全过 |
| test_r27_t05_experiment_integrity（#1768/#1770-#1787） | 34 / 7 | 全过 | 全过 |

## 受影响面回归 ×2（与上同批双跑，一致）

test_grader_engine 320/17 ✅ · test_grader_schema 241/14 ✅ · test_r16_a_grader 14/3 ✅ · test_r18_e_grader_faultlab 57/9 ✅ · test_experiment_evaluation 587/34 ✅ · test_experiment_consistency_r4 298/11 ✅ · test_placeholder_grammar 47/2 ✅ · test_repair_planner_completion 484/36 ✅（#1774/#1778 回归） · test_workflow_repair 102/9 ✅ · test_contract_projection_9 3473/9 ✅ · test_contract_platform_9 25/4 ✅ · test_command_contract_9 208/6 ✅ · test_help_coverage 14700/9 ✅ · test_capability_surface_parity 252/3 ✅（补 qrc 后） · test_workflow_checkpoint_cache 1169/45 ✅（#1771 回归） · test_r25_b_replay_deviation_v2 56/2 ✅（#1772 语义） · test_adversarial_m6 50/6 ✅（#1736 真场景） · test_paths ✅ · test_chunk_resume_11 47/8 ✅ · test_obia_architecture 12 ✅ · test_agent_ops_task_center_executor 101/28 ✅（#1779/#1780 执行路径） · test_classification_task_center 24/6 ✅ · test_teaching_admin_script_adapters_r8 7 ✅（SKIP 可见）

## RED 证明（修复撤销后失败）

- **#1744/#1773/#1777 全族**：grader 4 文件回 BASE 重建 → `test_r27_t05_grader_window` **28/95 断言失败**（窗口满分、requiredStages 不门禁、全角不命中、否定误判分、冲突误报均复现）。
- **#1781**：BASE 下编译即证明结构差异（RegressionMetrics 无 nonFiniteSkipped）；语义 RED 由 oracle 数学期望承担（NaN 混入→MAE 必然偏离 0.625）。
- **#1768**：BASE 下同 oracle 分类 New（代码路径 readable 计数仅 run_corrupt，可从 BASE 源静态证明）。
- **重放事故回归验证**：experiment_matrix.cpp 遗漏时 oracle 2 用例当场红（真实 RED→GREEN 闭环）。

## 已知非绿（如实记账）

| 项 | 状态 | 证据 |
|---|---|---|
| ep8 "JobEngine bucketed queue" 1 用例 | **PRE-EXISTING-BASE-RED** | 本轨 3 文件 checkout 回 BASE 重建后同样 14/15 失败×2；JobEngine（src/jobs）不在本轨白名单 → 归属轨处理 |
| #1697/#1698 冒烟（Win/mac CI） | NOT_EXECUTED_LOCAL | 本机 Linux；ci.yml 语法与逻辑静态核对 |
| #1732 ruleset 生效 | 不实施（D2） | RULESET_PROPOSAL.json + API 审计（404/空 rulesets）存档 |
| #1751/#1699 全量 sleep/时基改造 | PARTIAL | 只改白名单内清单点；505 处全量改造超本轨范围，记账 |

## 发布面本地验证

- `packaging/appimage-tool-checksums.txt`：两工具真实下载 sha256 与记录一致（c20cd7…/15106b…，19.4MB/12.3MB）。
- `build_offline_bundle.sh` / `build-appimage.sh`：`sh -n`/`bash -n` 通过；#1749 install-tree 分级逻辑就位（完整 assemble 需全量 install 产物，属 release 管道，未在本地跑全量）。
- `--version` 穿越（#1697，FIXED_IN_MASTER 复核）：`check_version` LC_ALL=C 白名单 + .cmd PowerShell 正则 exit 2 均在。
