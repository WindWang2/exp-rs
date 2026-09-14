# DECISIONS — r2-deep-review

## D-001 · 子代理 A 失败后的接管

- 事实：子代理 A（#953/#951 机械扫描区）首次调用模型请求失败；后台重试一次后
  captcha verify failed 而失败。
- 采纳：按 GOAL 自带默认（Subagents 段）由主线内联完成该区域审查，不再派第三次。
  记录于 EVIDENCE.md E-002。
- 理由：GOAL 明文已有失败默认；机械扫描区（tr() 审计）主代理可直接执行。

## D-002 · 去重策略（API 限流后的调整）

- 事实：`gh api search/issues` 关键词去重在第 3 个查询后停滞（限流/延迟）；已完成的
  2 个查询（class_mapping → 0、srcCrsOverride → 0）支持"无重复"结论。
- 采纳：改用本地去重三支柱：(a) R0 的 DEDUPE 基线（#595–#945，R0 已逐条对照）；
  (b) 时间 fencing——#951–#956 全部 2026-09-13 首次合并，不可能被基线覆盖；
  (c) R0 findings/operators.md + pi.md 的逐条 HEAD 复验。
- 理由：时间 fencing 对新建文件是决定性去重证据；对 R0 遗留 2 条当场复验代码未变。
  每条 issue body 的 Dedup 行写明所用支柱。

## D-003 · R0 F-PI-1 的降级处理

- 事实：R0 原文"pi 桥失步后不拆流"的确切语义已不可复原；HEAD 的 mcp_bridge 有坏行
  丢弃 + 续消费（自愈）、pending 走各自 timer。
- 采纳：不提交 issue，记入 dossier 撤下表。
- 理由：现有证据不足以支撑 P2 断言；按证据政策不提交推测性发现。

## D-004 · B4/B5/B7/B8/C4/D4/D5 的撤下

- 采纳：全部记入 dossier 撤下表（理由见 DEEP_REVIEW_R2.md）。
- 理由：置信度低 / 兜底在位 / 同源已被覆盖 / 非本仓库用法假设。

## D-005 · ADR 0146 九重复用进入 issue 而非仅 dossier

- 采纳：F2-11 以 documentation/P3 提交（#969），因其阻塞"按 ADR 号引用"的一切后续工作。
- 理由：编号仲裁是流程缺陷，issue 是正确的载体；重编号本身列为 follow-up。

## D-006 · 标签创建

- 事实：仓库已有 bug/documentation 等基础标签 + wayfinder 系列 + ready-for-agent，
  但无 needs-triage、无 severity 标签。
- 采纳：创建 `needs-triage`、`severity:P1`、`severity:P2`、`severity:P3`
 （颜色沿用 bug/quality 系）；沿用既有 `bug`/`documentation` 类型标签。
- 理由：与 docs/agents/triage-labels.md 对齐；severity 标签使 P0–P3 词汇可检索。

## D-006 · ADR 0146 numbering arbitration (issue #969)

Nine files shared `0146-*`. Kept canonical `0146-labspec.md` (earliest by git add). Renumbered the other eight sequentially after highest ADR 0149: 0150 lab-auto-grading, 0151 unified-rs-terminology-contract, 0152 lab-report-schema, 0153 offline-degradation-contract, 0154 capability-relation-graph, 0155 lab-copilot-teaching-constraint, 0156 spectral-library-material-priors, 0157 cn-product-adapters. Internal titles and cross-references updated.
