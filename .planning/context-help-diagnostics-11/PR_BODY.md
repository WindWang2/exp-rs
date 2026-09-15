# F20 · Context Help, Diagnostics & UX Guidance 11.0

**Local evidence only; no online CI dependency.** 9/9 targeted suites green after review fixes; final validation runs passed twice consecutively (R4+R5, 9 suites each, QT_QPA_PLATFORM=offscreen, test -j1).

## Baseline & ownership

- Baseline: `origin/master @ a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（分支基于此创建，PR 前 rebase 复核无新提交）。
- Open PR dedupe：
  - **#1009**（execution-11）改 `data/help/diagnostics.json` = 文件尾追加 2 个 operator 页；本 track 的 12 个新 harness 页全部插在文件**中段 harness 区**（≈行 1846），hunk 无重叠、可自动合并；其 `rs_operator_error` 新码本 track 未触碰。若 #1009 先合入：其 2 页自带 curated 页，合并后重跑 census gate 即绿（其页被 census 覆盖验证）。
  - **#1008**（spectral，CONFLICTING）：与 primary scope 零交集。
- Open issues #1001–#1007 均为 dataset/workflow/io/georef 域 bug，不属帮助体系，未认领（dedupe 记录见 BASELINE.md）。

## ⚠ Blocking master fix included (out of scope, minimal)

**master @ a5b11b7f10 无法编译 `sicnu_agent`**：#992（1ae474541f）在 `data_platform_tools.cpp` 引入 4 处未限定的 `BenchmarkService` / `benchmarkRunStatusToString`（`sicnu::experiment::` 域），全仓无 using-directive——GUI 与全部 `sicnu_add_test` 目标在 master 上编译失败。首个 commit（08ebe07270）按文件既有全限定惯例补全 4 处限定名，语义零变更。证据：修复前 sicnu_agent 构建日志（EVIDENCE.md）。

## 交付（按 WP）

| WP | 交付 |
|---|---|
| A census | 10 个缺失命令知识页（cartography.*×4、workbench.*×6）；`test_help_coverage` 命令词汇改用共享 `contracts/CommandRefScanner`（与 `test_command_contract_9` 单一 oracle；私有前缀白名单对 workflow.*/cartography.* 盲区消除） |
| B resolver | `ContextualHelpResolver`（src/app/help，纯函数）：detail topic + short tip + 机器 reason + 下一步 的单点组合 |
| C disabled reason | `ContextRules::requirementFacts` 成为 unavailabilityReason 与结构化 facts 的**单一派生**（facts↔enabled 同源零漂移）；`AvailabilityFact.code` / `AvailabilityExplanation.reasonCode` 机器 token（如 `raster.selected`）；手工 24 行静态表删除 |
| D error diagnostics | 12 个 harness 缺页补全（retry 值按 `retryClassForCode` 真值）；census oracle 升级为运行期 `allErrorCodes()` ∪ 扫描常量；未知 retry 值加载期 fail-closed（旧：静默降级 Derived）；非法 `"retryable"` 数据修正；`ErrorDiagnosticsBridge`（registry 驱动整词匹配，不猜码）；工作流失败面板 `helpId` 属性→F1 直达诊断页 |
| E i18n | `SicnuDialogHelp` 118 条全部译入 zh_CN.ts（#983 遗留闭环：ts 再生成早于 NOOP 改造、上下文 0 条）；`test_i18n` 新增 NOOP 上下文闭包 gate + 逐串翻译 gate + 占位符一致性 gate |
| F accessibility | 禁用原因三通道：statusTip、`disabledReason`/`disabledReasonCode` 属性、ribbon 按钮 accessibleDescription——tooltip 不再是唯一载体 |
| G generated reference | `docs/generated/help` 5 页 zero-diff gate（字节比较；`SICNU_REGEN_HELP_DOCS=1` opt-in 再生）；gate 揭示并修复了 ~2k 行静默漂移 |
| H UX corpus | `test_ux_guidance_corpus`（9 case / 1066 断言）：已知码/未知码/禁用命令/空工作区/模型缺失/消息→主题桥接 × 中文完整性 × secret 形状扫描 |

另：glossary 迁至独立 `:/terms` 资源前缀——修复 master 上 `test_help_core` 红（glossary 被内容店当知识页加载、307 条 "invalid id ''"）。

## 兼容性

- 数据 schema 纯 additive：commands.json +10、diagnostics.json +12（related 修正 1 处悬挂引用 `workbench.plugin_manager`→`command.workbench.operatorCatalog`，master 上即悬空）。JSON/Markdown 均为生成/声明面，无 API 破坏。
- 行为变更（有意、均有 gate）：①未知 retry 值从静默降级改为加载错误；②facts 通道从静态表变为 requirementFacts 投影（registry/palette/ribbon 的 enable 判定不变）；③工作流失败消息追加帮助行（仅当消息含 curated 码）。
- `test_help_core` 可用性呈现测试的事实构造改 3 参（新 code 字段）；无既有断言弱化。

## Review

独立对抗 review（subagent，只读，全 diff）：**P0=0、P1=1、P2=2、P3=12**；P1（obia 谓词误读——审计把 rasterTool lambda 的谓词归给 obia）全部撤销并更正文档；P2 全修；P3 逐条 disposition（REVIEW_LOG.md）。

## Local tests / evidence

| 套件 | 断言 | 基线（master） | 终态 |
|---|---|---|---|
| test_help_core | 2150 | **RED**（exit 42，glossary 307 错） | 绿 |
| test_command_contract_9 | 190 | **RED**（10 命令缺页） | 绿 |
| test_diagnostics_contract_9 | 170 | **RED**（7 码缺页） | 绿 |
| test_i18n | 401 | 绿（无 gate） | 绿（+2 gate） |
| test_diagnostic_report | 20 | 绿 | 绿 |
| test_help_system / test_global_help_tips | 78+25 | 未构建（master 编译损坏） | 绿 |
| test_help_coverage | 12337 | 未构建（同上）；源码级推导红 | 绿 |
| test_ux_guidance_corpus（新） | 1066 | — | 绿 |

R4+R5 连续两遍全绿（Oracle 双验证）。资源：build `-j2`、test `-j1`、offscreen；全套件墙钟 <3 min。数据/日志：`.planning/context-help-diagnostics-11/`（BASELINE/EVIDENCE/TEST_MATRIX/PERFORMANCE/REVIEW_LOG/DECISIONS）。

## Known limitations / follow-ups

- 帮助 JSON 内容保持中文优先契约（help_descriptor.h:10）；英文版 = 独立本地化工程，未启动。
- HelpCenterDialog / HelpViewerDialog / SicnuDialogHelp 三帮助面的查看器合并 = follow-up（本 track 完成了 SicnuDialogHelp↔ts 关联与 i18n 闭环）。
- WorkbenchGuidance 组件仍无消费方（涉及 workbench 布局面，登记 OUT_OF_SCOPE）。
- diagnostic 页 retry 派生语义（Derived）仍无运行期解析实现（master 既有，REVIEW_LOG #10）。
- bridge 的 UPPER_SNAKE/CamelCase 候选匹配对未来短码（如缩写）有理论误链面（REVIEW_LOG #2）。

**不 merge；不等待在线 CI。**
