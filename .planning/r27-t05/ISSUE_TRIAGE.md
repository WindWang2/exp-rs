# R27-T05 Issue Triage

依据：HEAD=`10652fd0` 代码证据逐条核对（2026-11-02）。状态含义：**FIX**（本轨修复）/ **FIXED_IN_MASTER**（已修，仅记录证据）/ **REDIRECT**（白名单外，留证据）/ **AUDIT**（只审计不改）/ **PARTIAL**（部分修复，如实记账）。

## P1

| Issue | 代号 | 判定 | 位置与证据 |
|---|---|---|---|
| #1744 | WE-11 | FIX | `src/grader/grader_matcher.cpp:450-486` AtLeast/AtMost 线性窗：band 失败值经 clamp 可得 1.0（如 AtLeast thr=0.8 win{0.1,0.5}，value=0.7→满学分）；loader（`grader_types.cpp:234-236`）只查 from<to 不查窗口相对阈值位置 |
| #1734 | BCT-03 | FIX | `.github/workflows/ci.yml:445-451` pwsh 断言 exit 2 后 `$LASTEXITCODE=2` 残留 → GHA pwsh 模板尾部 `exit $LASTEXITCODE` 使步骤失败 |
| #1735 | BCT-04 | FIX | `tests/test_r21_e_studio_verifier_hook.cpp:132` `INFO(... << observed)`（QJsonArray）无 ostream/StringMaker → Tier1/Tier2 全测编译失败 |
| #1736 | BCT-05 | FIX | `tests/test_adversarial_m6.cpp:216` `CHECK(listRecipes().size() >= 0)` 无符号恒真（#1558 残留）|
| #1737 | BCT-16 | FIX | 7 个契约/覆盖漂移测试（projection_9:526、platform_9:107、help_coverage:187,197、command_contract_9:274、capability_surface_parity:163,166、r22_data_security:440）被 BCT-04 遮蔽；需构建后逐个跑出失败再修 |

## P2 workflow-experiment（src/workflow、src/experiment、src/grader）

| Issue | 代号 | 判定 | 位置与证据 |
|---|---|---|---|
| #1768 | WE-05 | FIX | `src/experiment/repeat_execution.cpp:201-225,252` byRef 仅在 run_corrupt 计 unreadable，全 vanished 误判 New |
| #1769 | WE-06 | **REDIRECT** | `src/agent_loop/session_journal.cpp:309-311`（rdbuf 无字节上限）——白名单外，属 agent 轨（t03）生产目录；本轨不动，PR 中留证据 |
| #1770 | WE-07 | FIX | `src/experiment/lineage.cpp:73,89-92` 深度上限退出时未置 budgetExhausted |
| #1771 | WE-08 | FIX | `src/workflow/workflow_checkpoint.cpp:187-197` size-then-readAll TOCTOU（选举路径 :373 已用 read(cap+1) 正确惯用法）|
| #1772 | WE-09 | FIX | `src/experiment/replay_deviation.cpp:231-235,271,65-68` environment 仍 identityPin=true 强制 Deviated，与 remediation 文案"reported, not punished"矛盾 |
| #1773 | WE-12 | FIX | `src/grader/grader_engine.cpp:247-248` requiredStages 只评分不门禁 |
| #1774 | WE-13 | FIX* | `src/repair_planner/repair_planner.cpp:296,318,595` size_t 下溢 -7 参与 digest。*白名单未列 src/repair_planner，但里程碑 C 明确指派本轨且无兄弟轨认领 → 修，见 DECISIONS#D1 |
| #1775 | WE-14 | FIX | workflow dispatch `resolvePlaceholderPort` 未知/大小写不符端口静默落默认 |
| #1776 | WE-15 | FIX | workflow 参数展开任意 `${env.X}`/`${UPPER}` getenv 且写入 provenance |
| #1777 | WE-16 | FIX | `src/grader/grader_matcher.cpp:697-749` 答案匹配 ASCII-only：无 NFKC/全角/CJK、substring+否定盲 |
| #1778 | WE-17 | FIX* | `src/repair_planner/repair_planner.cpp` 主家族排序覆盖 refusal-last。*同 #1774 注 |
| #1779 | WE-20 | FIX | workflow dispatch `${task.parent}` 多父绑第一个 |
| #1780 | WE-21 | FIX | dispatch 未解析 `$step.port` 原样下发 |
| #1781 | WE-22 | FIX | `src/experiment/evaluation.cpp:265-275` regressionMetrics 接受 NaN/Inf |
| #1782 | WE-23 | FIX | `src/experiment/evaluation.cpp:309-357` 混淆矩阵 toInteger() 非整数静默 0 |
| #1783 | WE-27 | FIX | `src/experiment/reproduction_bundle_import.cpp` verify-then-reopen TOCTOU |
| #1784 | WE-28 | FIX | 同上：data/ 内硬链接通过符号链接检查 |
| #1785 | WE-31 | FIX | `src/experiment/experiment_matrix.cpp:136-175` 重复轴角色后者覆盖前者 |
| #1786 | WE-32 | FIX | 同上：seed 轴 "1"/"01"/"+1" 去重漏；≥2^53 接受 |
| #1787 | WE-33 | FIX | `src/experiment/benchmark_runner.cpp:388-398` 弃权预测跳过不罚，准确率虚高 |

## P2/P1 build-ci / infra

| Issue | 代号 | 判定 | 说明 |
|---|---|---|---|
| #1748 | BCT-11 | FIX | `packaging/appimage-tool-checksums.txt` 两个 PENDING → 下载官方产物计算真实 sha256 钉死（网络可用时），否则 NOT_EXECUTED 如实记录 |
| #1749 | BCT-13 | FIX | `scripts/build_offline_bundle.sh` bin-only：补 libsicnu_*.so 分级 + 离线可解析验证 |
| #1750 | BCT-20 | FIX | 7 处 SUCCEED-冒充-skip 改 `SKIP("…")` |
| #1751 | BCT-22 | PARTIAL | 505 sleep/167 sleep-then-assert 分布 136 文件；本轨修白名单相关热点（#1699 交叉），其余如实记账 |
| #1732 | BCT-01 | AUDIT | master 分支保护/ruleset 实时 API 审计 + 可执行 ruleset 提案；**不实施**（会影响并行轨合并流，见 DECISIONS#D2），权限不足/不实施原因如实记录 |
| #1697 | — | 核实 FIXED_IN_MASTER | PR #1708 已带 `--version` 穿越防护 + ci.yml:445 冒烟断言 exit 2；再以代码复核 |
| #1698 | — | 核实 FIXED_IN_MASTER | PR #1708 已带 INSTALL_RPATH/worker 安装 + install smoke；再以代码复核 |
| #1699 | — | PARTIAL | 墙钟阈值断言清单；与 #1751 交叉，修白名单内最差样本 |
| #1682 | — | PARTIAL | Win/mac 编译-only：在两 tier 加最小 `--help`/`--version` 冒烟（本地不可测，标 NOT_EXECUTED_LOCAL）|
| #1683 | — | PARTIAL | CI 卫生：加顶层 `permissions: contents: read` 等最小项 |

## P3 汇总

| Issue | 判定 |
|---|---|
| #1804 [P3][workflow-experiment] | 挑与本轨修复直接相关项；其余记账 |
| #1800 [P3][build-ci-tests] | 同上（BCT-15 #798 近空断言、BCT-19 等，见正文）|

## REDIRECT 汇总

- #1769（WE-06）→ `src/agent_loop/session_journal.cpp` 属 agent 轨生产目录（历史由 agent 系 PR 维护，r27-t03 活跃）。本轨不动生产文件；如需测试侧防护由归属轨补。

## 增补（2026-11-02 实施 + 核实）

- **P0 新发现（不在 issue 列表）**：tests/CMakeLists.txt 追加区在 r25-C..r26-science 多次合并中被错切（3 个未闭合 `target_link_libraries(`，整块交错），master 上 CMake 解析失败——任何测试都无法 configure（比 #1735 更底层；#1735 的"编译失败"表述实为该损坏叠加 INFO 编译错）。已重建：以各轨分支 tip 为权威逐 target 提取重组，31 兄弟 target 零丢失，configure 通过。PR 中显著说明。
- #1697 **FIXED_IN_MASTER 已核实**：`build_offline_bundle.sh:51` check_version（LC_ALL=C 白名单字符集）+ `.cmd:212` PowerShell 引用传参正则 + exit 2 + ci.yml 冒烟断言（本轨 #1734 仅修冒烟自身 LASTEXITCODE 残留）。
- #1698 **FIXED_IN_MASTER 已核实**：根 CMakeLists.txt:267 全局 `CMAKE_INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN"`（#1698 注释段）+ `src/cli/CMakeLists.txt:268` sicnu_worker install 规则 + Win/mac install smoke。
- #1682 **PARTIAL-ALREADY**：Win/mac tier 已带 #1697 guard + #1698 install smoke（编译外最小冒烟）；全量测试受 GHA 360min 上限（ci.yml 文件内论证），不再扩面。
- #1683 **PARTIAL-ALREADY**：顶层 `permissions: contents: read` 已在 master；actions pin/lint 基线等子项 P3 挂账。
- #1748 **FIX**：原 pin `1-alpha-20240109` 上游已 404（新下载实证）→ 重 pin linuxdeploy `1-alpha-20251107-1` + plugin-qt `1-alpha-20250213-1`，真实 sha256 已记录并本地下载验证匹配。

## 增补 2（2026-11-03 验证期发现）

- **ep8 bucketed-queue 既有红灯**：`test_execution_plane_8` 的 "JobEngine bucketed queue: priority pick and exclusive drain order" 1 用例在 **BASE 生产代码**（本轨 3 个相关文件 checkout 回 10652fd0 后重建）下同样 14/15 失败（×2 确定性复现）→ **PRE-EXISTING-BASE-RED**，非本轨回归；JobEngine/src/jobs 不在本轨白名单，记账留给归属轨。同套件其余 12 用例（含 worker 回收升级、TaskCenter 执行器路径）全绿（36106/36107 断言）。
- **RED→GREEN 重放事故与恢复**：验证期一次 `git checkout HEAD -- src/ tests/` 意外丢弃了当时未提交的 D 批生产修复与部分测试编辑（HEAD 只含前 4 个提交）。已按上下文逐文件重放并分片提交（5c387c770c / bf0c3dd46f / bb742edc1d）；重放遗漏的 experiment_matrix.cpp 被本轨 oracle 当场抓红后补提交。全部套件双跑一致全绿。
