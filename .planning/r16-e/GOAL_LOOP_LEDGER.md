# R16-E Goal Loop Ledger（Oracle 判定记录）

> 本文件是 R16-E（help+i18n / build+packaging 双轨）的主台账。每个阶段结束时追加判定，不删改历史条目。子轨道另有 `.planning/r16-e-t09/GOAL_LOOP_LEDGER.md` 与 `.planning/r16-e-t10/GOAL_LOOP_LEDGER.md`。不写全局 `.goal-loop-ledger.md`（避免与并行 Agent 冲突）。

## 阶段 0 — 基线、排重和风险清单（2026-10-17）

- [x] `git fetch origin --prune`；origin/master = `46791bc65c6f552ebbcf723bb91892736d56f319`，与参考 SHA 一致。
- [x] 开放 PR 5 个（#1513 Draft、#1514、#1515、#1516、#1517——#1517 为 prompt 快照后新增，含 help 文件重叠，已纳入冲突矩阵）。
- [x] 开放 Issue 25 个，逐一分类（ISSUE_PR_BRANCH_MATRIX.md）；7 项 headline 已核验 master 现状：#1490/#1495/#1494/#1468(三项) 已修复，#1489 部分修复，#1465 terminology 已修复待回归。
- [x] 远程分支 22、本地 worktree 17 分类；R16-A/B/C 并行 Prompt 的树与分支全部只读。
- [x] 3 个 R16-E worktree 建立（t09/t10/integration），分支名无冲突。
- [x] 产物：BASELINE.md、ISSUE_PR_BRANCH_MATRIX.md、OWNERSHIP.md、RISK_REGISTER.md。
- **Oracle 判定**：PASS——所有"将修"项都有 master 现状证据；无凭空接受的 Issue。

## 阶段 1 — 工作包分解

（待双轨子代理完成代码审阅后填充：每轨道 ≥4 个 WP，含 Oracle、预算、优先级。）

## 阶段 2 — RED→GREEN 执行

（待填充）

## 阶段 3 — 集成与对抗评审

（待填充）

## 阶段 4 — PR 与报告

（待填充）

## 阶段 2 — 双轨 RED→GREEN（2026-10-17）

- T10（5 commits，源分支 hardening/r16-e-t10-build-packaging）：WP1 #1491 本地裁决=不复现（本地 14/14 + CI run 38843229045 同套件 14/14）；WP2 census 子目录盲区 RED→GREEN；WP3 离线包可执行位缺陷 RED→GREEN（conformance 20 检查）；WP4 lupdate+src/python；WP5 preset binaryDir 唯一性 RED→GREEN。
- T09（6 commits，源分支 hardening/r16-e-t09-help-i18n）：WP1 163 条 curated diagnostics 空 summary RED→GREEN×2；WP2 .ts 1 条再生成消息补译 + freshness gate（FAIL→PASS×2）；WP3 JSON 深层嵌套 Json::LogicError 逃逸（exit 42）→ stackLimit+typed error GREEN×2；WP4 搜索 3000 查询 73,367ms→11,273/10,802ms（×2，6.5×，RSS 138.8→123.5MB）；WP5 #1465 回归钉 GREEN×2。
- 注：T09 首次启动因基础设施故障（off-peak-ticket-expired）未运行，重试成功；其台账重写时发现前一遗留会话有未落盘的虚假 GREEN 声明，已全部以本会话实测覆盖。

## 阶段 3 — 集成与回归（2026-10-17）

- cherry-pick 全部 11 个双轨 commit 到 hardening/r16-e-help-delivery，零冲突（文件集不相交，符合 OWNERSHIP 合同）。
- 跨轨合同闭环 commit `05f22ec96`：src/python 纳入 lupdate 后补译 SicnuPythonConsole/"Enter Python code..."，freshness gate 集成树实测 PASS（tracked 23561 == regenerated 23561，unfinished=0，new=0）。
- 集成树脚本回归：census exit 0；check_presets ALL PASS；bundle conformance 20 checks 0 failed（PS lane 因无 pwsh not-executed，如实记录）。
- 集成树构建（build-r16-e-int，-j4+锁，293 目标约 4 分钟）+ ctest：7/7（test_r16_E_help_data_integrity/ parsing_robustness/ search_perf/ terminology_regression/ i18n_translation_freshness + test_help_commands_schema + test_i18n_migrated_translations）；主钉直跑：test_i18n 401 断言、test_help_core 2156 断言 全过；io_vector_contract 14/14。
- **GREEN×2**：7 测试族第二轮 30.56s 再次 100%。
- 待办：两轮独立评审 → PR。

## 阶段 3（续）— 对抗评审两轮（2026-10-17/18）

- **第 1 轮**（独立 Reviewer，未参与实现；完整 diff 审计）：1×P0 + 7×P2。
  - P0-1：test_r16_E_help_search_perf 15s 墙钟断言在 CI tier2 ASan+UBSan 下确定性失败（reviewer 实测 40445ms）→ 修复 `47fbc8e39`：宏化预算 + ENABLE_SANITIZERS 注入 52000/35000（plumbing 经 scratch configure 的 ninja -t commands 证明）；determinism 断言不缩放。
  - P2×5 同 commit 修复（freshness 解析 fail-loud + src/python fallback + --dirs；QSet 去重；family 排序遍历；census basename 模板匹配；注释清理）。P2 未修项：golden-vector（reviewer 已外部证明 3834 组逐位等价，记录于 PR 正文）；重链测试 2 个预存失败（master 侧根因，见 PR 正文）；4 个 Catch2 目标裸 add_test 惯例偏差（与既有 21 处 python 门禁一致，记录）。
  - Reviewer 正面结论：产品代码零正确性缺陷；#1517 所有三文件零触碰；163 计数属实；cherry-pick 13 文件逐字节一致。
- 修复后回归：7/7 ×2（34.03s/34.76s）+ 脚本四件套全绿。
- **第 2 轮**（新独立 Reviewer，回归与边界复审）：**0 未解决 P0/P1 成立**；5 项第 1 轮修复逐一 PASS（含 --dirs 短路、constFind 无 UB、falsy 陷阱路径分析）；边界二次扫描无新 P0/P1；台账数字与 HEAD 实测全部吻合（无虚报）；PR 就绪检查全过。
  - 新增 2×P2：printf %d/qsizetype UB、sanitizer 预算 22% 裕量提示 → 前者修于 `b40ea9e10`（+%lld cast 与 errors="replace"），后者+代理对截断观察建 Issue **#1522** 跟踪。
  - 修复后验证：perf/freshness 2/2（20.47s）、gate PASS、selftest PASS。

## 阶段 4 — PR 与报告（2026-10-18）

- 残留 P2 → Issue #1522。
- 终验：git diff --check 干净、status 干净、17 commits。
- 推送 hardening/r16-e-help-delivery → gh pr create（正文含 baseline SHA、双轨完成表、测试证据、性能数据、两轮评审结论、协调项）。
