# CAPABILITY MATRIX — before/after（master a5b11b7f10 → 本 track）

| 能力 | Before（master） | After（本 track PR） |
|---|---|---|
| 命令→帮助知识 census | 双扫描器漂移（workflow 前缀漏扫）；10 命令疑似无页 | 单一共享 scanner（contracts/command_ref_scanner）；全命令页 + 反向孤儿 gate |
| harness 错误码→诊断页 | 7 新码无 curated 页（gate 红） | 全码 curated（按 retryClassForCode 真值填 retry），census 绿 |
| retry 字段语义 | 未知值静默降级 Derived | 未知值加载期报错（fail-visible）；数据修正 |
| disabled reason 机器可读 | 静态 24 条 facts 表（与谓词集恰好一致）+ 独立 reason 文本 | requirementFacts 单一派生（facts=reason 同源）+ 机器 reason code；表删除后无双维护 |
| GUI 错误→帮助 | 无接线（仅 agent lab） | 任务失败路径单点接线：code→whatHappened+remediation+helpId 指引 |
| 生成 reference gate | 无（仅内存两次相等） | zero-diff：重生成 vs docs/generated/help 逐字节 gate |
| i18n | SicnuDialogHelp NOOP 未入 ts；presenter 骨架硬编码中文未 tr() | NOOP 上下文→ts drift gate；骨架文案 en 源 + zh_CN ts；lupdate 范围一致 |
| 可访问性 | 禁用原因仅 tooltip 通道 | reason 同源写入 accessible 描述；a11y 契约断言 |
| UX 评测语料 | 无 | 场景表驱动 corpus（错误/禁用/空数据/离线/模型缺失×中文×无 secret） |
| workbench guidance 可达性 | 组件无消费方 | 登记为 known limitation（OUT_OF_SCOPE，涉及 workbench 布局面） |
| 帮助查看器三 authority 并存 | HelpCenter/HelpViewer/SicnuDialogHelp 无 ID 关联 | SicnuDialogHelp↔HelpId 最小关联契约（census 登记）；查看器合并 = follow-up |

## Not supported / degraded（诚实声明）

- 不提供英文版 data/help 内容（契约见 DECISIONS D5）。
- 不合并三个查看器。
- 不为 #1008 新增 spectral 工具提供帮助条目（其 PR 未合入，属其范围）。
