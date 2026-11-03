# R27-T05 Decisions（无人值守自动决策账本）

- **D1 · src/repair_planner 归属（#1774/#1778）**：白名单未列该目录，但里程碑 C 点名"repair planner 排序"且 oracle 含"修复排序"；`git log` 显示无 R27 兄弟轨认领（近史 r13-h），候选 issue 归属标签 [workflow-experiment]。判定：本轨修复（手术式最小 diff），PR 描述显著声明。备选（拒）：留 REDIRECT —— 与 oracle 直接冲突，且无轨认领将无限期悬空。
- **D2 · #1732 ruleset 不实施**：token 具备 admin 权限，但启用 master 必需状态检查会立即阻断并行 4 轨的合并流（跨轨干扰、不可逆面大）。判定：产出实时 API 审计 + 可执行 ruleset JSON 提案与证据，标注"maintainer action, deliberately not applied"；不算 BLOCKED_EXTERNAL（权限够），是范围决策。
- **D3 · #1772 environment 维度语义**：两个方向（a）environment 改 diagnostic-only（与 remediation 文案一致、与 #1547 意图一致）；(b) 只改文案保留惩罚。选 (a)：文案与判定矛盾源于 identity 集合收窄未完成；评分科学性优先。风险：依赖旧行为的既有测试需同步更新（以实际测试结果为准）。
- **D4 · #1787 弃权处理**：头条 accuracy 将弃权计为错误（弃权=不确定=未正确分类），raw_metrics 增加 abstain 计数与 coverage 保持可解释。不隐藏、不改名既有字段语义（n 语义 = 全部预测）。
- **D5 · 新测试注册**：统一前缀 `tests/test_r27_t05_*`，`tests/CMakeLists.txt` 末尾 append-only 块，标识 `# ── BEGIN R27-T05 ──` / `# ── END R27-T05 ──`。
- **D6 · 构建纪律**：全宿主 `/tmp/exp-rs-host-build.lock` flock，`--parallel 2`；configure 与 build 均持锁；测试运行不持锁（ctest -j1）。
- **D7 · RED→GREEN 证据**：每个 oracle 测试先在未修复代码上跑出失败（保留输出），再修复通过；关键 oracle 双跑。生产 hunk 用 `git stash push -- <file>` 临时回退实现 RED（只重编受影响 TU+测试）。
- **D8 · #1748 AppImage 哈希**：优先下载官方 release 计算真实 sha256 钉死；网络不可达则 NOT_EXECUTED 如实记录（fail-closed 语义保留）。
- **D9 · #1737 契约漂移**：以 BASE_SHA 实际生成为准更新期望表；若漂移源于生产代码 bug（非新增能力）则修代码。逐条记录判定理由。
- **D10 · #1750/#1751**：只改本轨白名单语义相关文件内列举位点；不为凑数跨轨改文件。
