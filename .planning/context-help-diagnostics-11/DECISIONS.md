# DECISIONS — F20 context-help-diagnostics-11

记录格式：背景 → 候选 → 决定 → 理由。所有决定遵守 autonomy defaults（保守、兼容、最少重复实现）。

## D1. 与 PR #1009 在 diagnostics.json 的共写规避

- 背景：#1009（open，MERGEABLE）在 diagnostics.json **文件尾** append 2 个 operator 页；本 track 也需向该文件追加 harness 页。
- 候选：(a) 文件尾追加（自然但与 #1009 必然文本冲突）；(b) 插入中段 harness 区之后（与 #1009 尾部 append 正交）；(c) 等其合并后再动。
- 决定：**(b) 中段插入**。理由：语义上 harness 页与既有 harness 区聚在一起更一致；文本上与 #1009 的尾部追加完全正交，rebase 零冲突。#1009 合入后其 2 页由其 census gate 自证。
- 状态：已执行（Phase 3 commit）。

## D2. 命令 census oracle 收敛到单一扫描器

- 背景：test_help_coverage（源码正则，前缀集合盲区）与 test_command_contract_9（contracts/command_ref_scanner.cpp，四种 idiom）是同一事实的两套扫描器，前者已有 workflow 前缀漏扫。
- 候选：(a) 让 test_help_coverage 复用 contracts/command_ref_scanner（单一真值）；(b) 修补 test_help_coverage 自己的正则。
- 决定：**(a)**。理由：消除双真值是根因修复；command_ref_scanner 已是 test_command_contract_9 的稳定 oracle 且 idiom 覆盖更全。test_help_coverage 保留其独有断言（覆盖率下限、curated 下限、availability 表存在性），命令全集改由共享 scanner 提供。
- 状态：已执行（Phase 1 commit）。

## D3. AvailabilityFacts 全量覆盖的实现路径

- 背景：AvailabilityFactsAdapter 用静态 24 条需求表，表外命令 explain() 空 facts → 误报 available。
- 候选：(a) 继续扩静态表；(b) adapter 直接调用 ContextRules::unavailabilityReason（已有纯函数、按命令 id 分派）派生 facts。
- 决定：**(b)**。ContextRules 是 enabled 状态的既有真值（CommandRegistry 也用它），adapter 从它派生 facts 保证 facts↔enabled 永不漂移；静态表删除。
- 更正（review #2 P1-1）：此前审计误判"静态表缺 workbench.obia"。实际 obia 在 command_defs.cpp **无** d.availability 谓词、恒可用（第 474 行命中属于 rasterTool lambda 定义）；旧 24 行表恰好完备。review 曾短暂给 obia 加 raster 特例，已按 review 撤销——facts 只镜像真实声明的谓词；corpus 对应场景改为正向断言"无谓词命令诚实报告可用"。requirementFacts 的真正价值是消除表↔reason 双维护与提供机器 reason code。
- 状态：已执行（Phase 2 commit）。

## D4. GUI 错误路径接入 DiagnosticCatalog 的最小接触面

- 背景：诊断目录唯一生产消费方是 agent/lab_diagnostics；GUI 错误呈现不显示帮助入口。
- 候选：(a) 改造所有错误弹窗；(b) 找到任务/错误的单一汇聚点（任务失败通知路径）接线一处，展示 code + 帮助指引。
- 决定：**(b) 单点接线**：在任务失败错误格式化处（现有错误码已知的位置）追加 DiagnosticCatalog::resolve 产生的指引行（whatHappened+remediation+「按 F1 / --help-topic <id>」），不新造弹窗体系。
- 状态：已执行（Phase 3/4 commit，接触文件见 diff）。

## D5. i18n 契约方向：不把 data/help JSON 改成双语

- 背景：data/help 全部中文单语；候选 (a) JSON 增加英文镜像字段（大规模、易漂移）；(b) 维持中文优先契约（help_descriptor.h 明示），只把**代码骨架文案**纳入 Qt 翻译体系（en 源文本 + zh_CN ts）并补 NOOP→ts drift gate。
- 决定：**(b)**。理由：repo 契约是中文优先 + Qt ts 服务于代码内英文源文本；JSON 双语化是 schema 大改且无消费方（UI 语言切换仅影响 tr() 文本）。E 包交付 = presenter/topic_text 骨架 tr() 化 + SicnuDialogHelp NOOP 上下文入 ts + drift gate（源码 NOOP 上下文 ⊆ ts 上下文）。
- 状态：已执行（Phase 5 commit）。

## D6. 生成物 zero-diff gate 的实现位置

- 背景：docs/generated/help/*.md 已提交、无 gate。
- 候选：(a) 测试里调用 HelpMarkdownWriter 重生成并与 docs/ 逐字节比较；(b) 独立脚本 + CI。
- 决定：**(a)**（本 track 无 CI 依赖，测试即 gate；ci=none）。写路径只读 docs/、不落盘；diff 失败时报首个差异文件与行。注意 qrc 嵌入资源在测试进程可用（test_help_coverage 已验证）。
- 状态：已执行（Phase 6 commit）。

## D7. 7 个新 harness 码诊断页的 retry 语义

- 背景：a966534ef5 引入 WAVELENGTH_INCOMPATIBLE 等 7 码，无 curated 页；其 RetryClass 已在 harness_error.h 定义。
- 决定：每码新增 curated 页，`retry` 字段**按 harness_error.h 的 retryClassForCode 真值**填写（非臆造）；插入位置遵守 D1。
- 状态：已执行（Phase 3 commit）。

## D8. retry 解析未知值 fail-visible

- 背景：help_content_store 只认 none/manual/transient，`"retryable"` 静默降级。
- 候选：(a) 加 "retryable" 到识别集；(b) 未知值在加载时报错误 + 修正数据为合法值。
- 决定：**(b)**：解析器遇到未知 retry 值返回错误（fail-closed，符合 #1000 后的 fail-closed 方向），并把 diagnostics.json 中非法值改为解析器认可的语义值（该条目 runtime_provider_failed 属 operator 家族，真值取 retryClassForCode）。
- 状态：已执行（Phase 1/3 commit）。

## D9. i18n drift gate 的形式

- 背景：#983 把 dialog_help_catalog 文案改为 QT_TRANSLATE_NOOP("SicnuDialogHelp",…) 存储但 ts 再生成早于该 commit，ts 中该上下文 0 条；且无任何 gate 阻止再次漂移。
- 决定：test_i18n 新增两段——(1) 上下文闭包：扫描 src/app+src/help 中所有 QT_TRANSLATE_NOOP 上下文，逐个断言存在于 sicnu_zh_CN.ts；(2) 上下文级完备：对 SicnuDialogHelp 用容错扫描器提取全部 NOOP 字符串（含跨行/相邻字面量拼接），逐条断言 ts 中存在同 <source> 且翻译非空，并对 %1..%9 占位符做 source↔translation 数量一致性检查。配套执行 lupdate（src/app 扫描范围扩为 src/app+src/help，CMake sicnu_i18n_update 目标同步修改）再生 ts。
- 状态：Phase 5 执行。

## D10. 生成物 zero-diff gate 的再生通道

- 背景：docs/generated/help/*.md 无 gate；再生的唯一途径是完整 GUI 二进制的 --export-help-docs。
- 决定：zero-diff 测试默认只读比较；当环境变量 SICNU_REGEN_HELP_DOCS=1 时改为写回 docs/generated/help/（opt-in 维护通道，普通构建/测试绝不写仓库）。知识变更的 commit 必须附带再生输出（同 commit 保持 zero-diff 绿）。
- 状态：Phase 6 执行。

## D11. 基线策略（构建时间 vs 证据强度的折中）

- 背景：全部 sicnu_add_test 目标默认链接 qgis_core/qgis_gui/processing 等重库，冷构建在 -j2 硬约束下需要小时级。
- 决定：基线分两级——(a) 实际运行捕获：test_help_core（RED，exit 42，glossary 307 错误）+ 轻闭包的 test_command_contract_9/test_diagnostics_contract_9/test_i18n/test_diagnostic_report；(b) 源码级确定性推导：test_help_coverage（反向检查对 command.workflow.* 必红：扫描前缀集合无 workflow，commands.json 有 5 条 workflow 条目——逻辑上无逃逸路径）。重目标以终态双跑为实证。
- 状态：已记录；终态证据见 TEST_MATRIX。

## D12. AvailabilityFact 增加 code 字段（机器可读 reason token）

- 背景：C 包要求 machine-readable reason；现有 facts 只有 label（句子）+satisfied。
- 决定：AvailabilityFact 增加 `QString code`（稳定 snake token，如 "raster.selected"），AvailabilityExplanation 增加 `QString reasonCode`（首个未满足事实的 code）。code 由 ContextRules::requirementFacts（新增，单一派生函数）产出——unavailabilityReason 与 facts 同源零漂移（DECISIONS D3 的实现载体）。

## Rescope 记录（对照原 WP 表）

- WP A「glossary 全量映射」：terminology（307 条）已有 provider+索引+test_i18n 覆盖，本 track 不重做，仅在 census 文档记录其 authority。
- WP B「active surface」：不新增 GUI surface；resolver 以纯逻辑库交付，GUI 现有 HelpSystemController 消费 facts 的通道保持，新增消费仅限 D4 单点。
- WP F「high-DPI」：repo 无 high-DPI help panel 缺陷证据，不引入；focus/shortcut/accessible name 的机器断言覆盖到本 track 触及的组件为止（OUT_OF_SCOPE 记录其余）。
