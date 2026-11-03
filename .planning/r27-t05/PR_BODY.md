# r27-T05: 工作流/实验复现/教学评分闭环 + 测试通道与发布质量（P0 修复 + 22 项 issue）

**Base**: `10652fd003` (= origin/master at 2026-11-02T16:25Z) · **Head**: `b96321f7fca04383f8f9713776f0471773dde5ec` · 单一 worktree 分支，未合并任何 PR。

## ⚠️ 先读：一个 P0 发现（非 issue 清单内）

`tests/CMakeLists.txt` 的追加区在 r25-C→r26-science 的多次合并中被错切（3 个未闭合 `target_link_libraries(`、整块交错）——**master 上 CMake 解析失败，任何测试都无法 configure**。#1735 的"INFO<<QJsonArray 编译失败"只是叠加在其上的第二层。本 PR 以各轨分支 tip 为权威逐 target 重建该区（**31 个兄弟轨测试 target 零丢失**，括号平衡校验、configure 通过）；此文件为共享资源，请各轨维护者重点 review `tests/CMakeLists.txt` 的 diff。

## 覆盖 Issue（26 项）

| 类 | 修复 | 摘要 |
|---|---|---|
| P1 评分 | #1744 | AtLeast/AtMost 线性窗带外值经 clamp 得满分的 bug：ceiling=(to−from)/(bandEdge−from)，短窗封顶；loader 拒绝跨带/错侧窗口（全仓无受影响 rubric：data/ 零 linearWindow） |
| P1 评分 | #1773 | requiredStages 未满足时 verdict 降为 Partial，不再假 Pass |
| P1 测试通道 | #1734/#1735/#1736/#1737 | pwsh LASTEXITCODE 残留、QJsonArray 编译错、空断言→移植 e2e 真场景、7 个被遮蔽的契约漂移测试逐个修复（chunked-fabric allow-list + CLI↔help 漂移记账 OWNED-BY-CLI-DRIFT + parity 门禁补 qrc + 快照/文档按文档化程序再生成） |
| P2 工作流 | #1775/#1776/#1779/#1780/#1771/#1770 | 未知端口不再静默落默认输出；env 展开 allow-list 化且密钥形名称硬拒；多父 `${task.parent}` 拒绝歧义绑定；未解析占位符派发前类型化拒绝；checkpoint cap+1 读消 TOCTOU；lineage 深度截断置 budgetExhausted |
| P2 实验 | #1768/#1781/#1782/#1785/#1786/#1787/#1772/#1783/#1784 | byRef 全不可读→拒绝而非误判 New；回归指标滤 NaN/Inf 并计数（全非有限→拒绝）；混淆矩阵单元格严格化；重复 pin 角色拒绝；seed 规范化+2^53 对齐；弃权计入头条准确率分母（accuracy_scored_only 并列暴露）；environment 改诊断维度与文案一致；bundle 导入读-验-写消 TOCTOU+拒硬链接 |
| P2 修复面 | #1774/#1778 | repair_planner 下溢归零；refusal-last 排序优先于主家族偏好 |
| P2 教学 | #1777 | 答案匹配：全角折叠、CJK、否定感知（中英文否定词表）、冲突检查归一化 |
| P2 发布 | #1748/#1749/#1750 | AppImage 工具重 pin（原 tag 上游 404，实证）+ 真实 sha256（下载验证）；离线包 install-tree 分级 + lib 兜底；8 处 SUCCEED 冒充 skip → SKIP() |
| P0 审计 | #1732 | master 无分支保护/rulesets 的实时 API 证据 + 可执行 ruleset 提案（`RULESET_PROPOSAL.json`）。**刻意未实施**（D2：会立即阻断 4 条并行轨合并流；token 权限足够，属范围决策非 BLOCKED_EXTERNAL） |

**FIXED_IN_MASTER（核实证据，未重复实现）**：#1697（check_version 白名单+.cmd 正则 exit 2+CI 冒烟）、#1698（全局 INSTALL_RPATH + sicnu_worker install 规则 + Win/mac install smoke）。
**REDIRECT**：#1769 → `src/agent_loop/`（agent 轨生产目录）。
**PARTIAL 记账**：#1751/#1699（505 sleep 全量改造超轨范围，仅白名单内清单点）、#1682（Win/mac 已有最小冒烟，全量测试受 GHA 360min 上限）、#1683（permissions:read 已在 master，余项 P3）。

## 证据

- **Oracle 新增 3 套**（17 用例 / 140 断言）+ 受影响面回归 23 套，**全部双跑逐字节一致全绿**（TEST_RESULTS.md：含 14700 断言 help_coverage、3473 断言 projection_9、36107 断言 execution_plane_8 等）。
- **RED 证明**：grader 族回退 BASE 后 28/95 断言失败；matrix 重放遗漏被 oracle 当场抓红。
- **ep8 bucketed-queue 1 用例**：BASE 对照（3 文件 checkout 回退重建）同样失败 → **PRE-EXISTING-BASE-RED**，非本轨回归，JobEngine 归属轨。
- #1748：两工具真实下载哈希与记录一致。

## 边界声明（写入白名单外但必要的路径）

`src/repair_planner/**`、`src/processing/framework/task_center.cpp`（里程碑 C 点名 dispatch/repair planner + 无兄弟轨认领，DECISIONS D1/D12）；`docs/generated/help/*`、`data/contracts/contract_graph.snap.json`（#1737 漂移门的受控生成物，经文档化程序再生成）；`.gitignore` 仅追加本轨 `.planning/r27-t05` allow-list（每轨先例）。

## R1/R2 独立评审

- **R1（对抗性）**：4 findings——F1 **P1** 混淆矩阵 qint64 越界转换 UB（已修，范围门+重测）；F2 P2 launch gate 残留 ready-serial 致 aging sweep 空扫（已修）；F3/F4 P3 否定检测仅前缀窗口、字面 `$step.` 参数将被拒（文档化限制）。**tests/CMakeLists.txt 重建完整性机器审计：BASE 953 target / HEAD 956，零丢失**。
- **R2（换视角：issue 原文↔代码证明 + 记账生命周期）**：2 findings——F1 **P1 流程** rot guard 首版作用域错误被同批 GCC ICE 吞掉、旧二进制假绿（已修：集合提升文件级，断言 14700→14716 实增证真；流程教训入账：断言计数不随补丁变化即视为未重编）；F2 P3 三处 CLI 漂移记账集合补 rot 防护（已修）。
- **评审后关键 oracle 12 套件重跑 ×2 逐字节一致全绿**（TEST_RESULTS.md 增补）。
- 隔离声明：两轮因子代理配额受限由主代理内联执行（方法论独立+机器审计），全程留痕于 REVIEW_R1/R2.md。

## 平台与外部项

Linux 本地全量验证；Windows/macOS 逻辑静态核对未执行（NOT_EXECUTED_LOCAL）；未等待远程 CI（按轨规则）；ruleset 未实施（见上）。
