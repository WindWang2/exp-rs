# 执行摘要 — R1 · Prompt & Command Hygiene Review

Track: `zcode/prompt-command-hygiene-review` · worktree `../exp-rs-prompt-command-hygiene-review` · base origin/master @ 60179408 · 2026-09-13

## 一句话结论

`/goal` 已在 24 个实战 track 上形成一套稳定的事实标准（master 只读 + worktree、≤2 只读子代理、
-j2 家族、无 CI、PR 即终点、P0/P1=0 审查门），但它从未落盘成规范——本 track 把它固化成三份
自包含文档（goal-template / loop-template / command-vocabulary），并修掉了指令体系里全部 7 处
断链引用、把 /goal 与 /loop 的边界判据第一次写进了仓库。

## 交付物

| 产物 | 内容 |
| --- | --- |
| `docs/agents/goal-template.md` | /goal 唯一事实来源：参数头、可复制骨架、九阶段预算范式、7 类必答 Autonomy defaults、10 步 PR runbook、存在性断言、硬性自查清单 |
| `docs/agents/loop-template.md` | /loop 唯一事实来源（标注：新约定，仓库尚无实战先例）：Trigger/Checkpoint/Push right/Brief + 跨轮游标 + 反模式表 |
| `docs/agents/command-vocabulary.md` | 四命令选择判据图、"做完还要再做一次吗"根判据、3 个真实历史 track 归类、P0–P3 严重度共享词表 |
| `review/GOAL_MATRIX.csv` | 24 个历史 GOAL.md × 24 个约定维度的穷尽矩阵，逐格 文件:行号；另录 12 个无 GOAL.md 的 track |
| `review/PROMPT_DEFECTS.md` | 28 条缺陷（五透镜，每条带逐字引用）+ 事实标准约定归纳（L1/L2/L3 分级）+ 4 条撤下记录 |
| `review/SKILL_INVENTORY.md` | 技能真实清单：karpathy-guidelines 缺失确认、planningwithfiles/gstack 仅用户级、matt=ask-matt 已双侧镜像 |
| `review/SKILL_MIRROR.md` | .agents × .claude 镜像状态：37 共有字节一致、13 个供应商技能单侧、规则与复核命令 |
| `.agents/AGENTS.md`（修复） | 断链出处删除（改指 CHANGELOG 2026-08-03 条目）、C++17→C++20（以 CMakeLists.txt:3 为权威）、与 CLAUDE.md 互引、unattended 适配句 |
| `CLAUDE.md`（修复） | Quick Commands 重写为 CMakePresets + -j2（原 `make -j$(nproc)` 违反资源上限且路径失效）、`.agents/vendor/` 断链删除、"mirrored" 措辞改为与实测一致、与 AGENTS.md 互引 |
| `.gitignore`（1 处） | 本 track 白名单条目（机械必需，lab-spec 先例模式；同时暴露了系统性缺陷 D-005） |

## 关键发现（top 5）

1. **规范层整体缺位**：24 个 GOAL.md 无模板、无词汇表；7 个 GOAL.md 引用仓库外的 "goal brief §N"；
   R0 的 binding GOAL 全文从未落盘。12/36 个 track 目录连 GOAL.md 都没有。
2. **规划文件会被静默丢失**：`.gitignore:119` 的 `.planning/*` + 白名单机制吞掉新 track 的规划产物
   （实测本 track 自己中招；spatial-scientist-harness-8 因此丢过全部原始规划文件）。模板 runbook 已
   加白名单自检步。
3. **双运行时准则漂移成矛盾**：AGENTS.md（C++17、死链）与 CLAUDE.md（C++20、`make -j$(nproc)`、
   `.agents/vendor/` 断链）零互引、互不同步，已在语言标准与资源上限上直接冲突。已修复并互引。
4. **命令体系与技能体系互不相认**：24/24 个 GOAL.md 零技能引用；autonomy=full 与 grilling 的提问
   义务无裁决规则。模板已建立 Skills 段与优先级/冲突裁决。
5. **/loop 是零落地的新约定**：无先例、无产物目录；模板按"推导的新约定"如实标注证据等级，词汇
   取自已落盘的 loop-me 技能。

## 交叉复核结果

- **子代理 A（考古）**：独立重读全部 GOAL.md，修正主代理 1 处归纳（分支前缀是时代分布而非"现行多数"），
  贡献 3 条新缺陷（D-026 sicnu_cli、D-027 ADR 0144 三重复用、build.cmd 死路径并入 D-028），确认其余
  归纳无遗漏。逐条裁决见 REVIEW_LOG.md Phase 5。
- **子代理 B（对抗）**：用新模板驱动假想 track 与假想循环，找出 30 处会导致提问或走偏的漏洞
  （最致命：AGENTS.md"先澄清"与 autonomy=full 无裁决、runbook 白名单步顺序死锁、PR 被拒零处理、
  整库审查循环被反模式表误伤）。30 项全部修复进模板，无一遗留需要向用户提问的缺口。

## 假阳性率

提交缺陷 28 条，撤下 4 条（R-1 技能漂移已被修复、R-2 并入 D-007、R-3 非仓库内断链、R-4 无矛盾），
**撤下数/提交数 = 4/28 ≈ 14.3%**。撤下原因逐条见 PROMPT_DEFECTS.md Part III。

## 边界与合规

- `src/`、`tests/` 零改动（PR diff 可核）；未编译、未测试（纯文档审查，资源日志不适用）。
- 全程无 CI 依赖；未建远端 issue；`gh issue create` 未执行。
- UNDETERMINED 共 5 项，全部裁决或标注为不阻塞（REVIEW_LOG.md Phase 7）。
- AUDIT_DOSSIER_ISSUES_747_760.md / PROJECT_REVIEW_DOSSIER_5.0.md 为 src 级代码审查，与本 track 零重叠（E-005）。

## 留给后续 track 的种子

1. D-027 ADR 编号冲突重排（domain-modeling 体系）。
2. D-028 根目录历史构建脚本清理（build.cmd / configure_wb7.cmd / build_wb7.cmd）。
3. /loop 的首个实战 track（建议从"master 前进后重跑三透镜审查"或"issue 重定位"选一），
   以验证 loop-template 并把它从"推导约定"升级为"事实标准"。
4. 12 个无 GOAL.md 的历史 track：如需审计合规，按新模板补落盘自述件（标明 reconstruction）。
