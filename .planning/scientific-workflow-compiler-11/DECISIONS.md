# DECISIONS — Scientific Workflow Compiler & Grounding 11.0

每条：背景 → 候选 → 选择 → 理由。新增决定追加在文件尾部，不改历史条目。

## D-001 preset 名称（GOAL 笔误裁决）

- 背景：GOAL 写 `CMakePresets.json/build-dev`；实际 configurePresets = dev-default/ci-fast/ci-full/sanitizer-debug/release-package（BASELINE.md 事实 14）。
- 候选：a) 找/造 build-dev；b) 用现有 `dev-default`。
- 选择：b。理由：`build-dev` 不存在于 master；造新 preset 违反最小接线。资源上限（-j2/-j1）照 GOAL 执行。

## D-002 #991/#992 已合并后的 ownership 处理

- 背景：prompt 快照假设 #991/#992 open；启动审计发现均已合入 master（BASELINE.md）。
- 候选：a) 仍按 open-PR 排他处理 src/workflow/**、D19 文件；b) 完全解禁。
- 选择：折中——src/workflow/** 全程只读（执行平面权威在 #1009 系与引擎 owner，本 track 只经 `compilePlanToWorkflowJson` metadata seam 投影）；D18/D19 的 IR2/foundry 功能不重写不复制。理由：E 包"交给已有执行入口"的本意就是不改执行平面；#1009 仍在改 `src/workflow/pipeline_run_coordinator.cpp`，保持只读可零冲突。

## D-003 新事实/新检查放哪（无第二真值）

- 背景：A/C 包需要新事实与新检查；既有 authority = `band_facts`(单景物理事实)、`workflow_analysis`(检查)、`harness_error`(错误码)、`grounding_tools`(understanding)。
- 候选：a) 全部塞进 band_facts/workflow_analysis 现文件；b) 新模块 `workflow_facts.*`、`grounding_probes.*`、`provenance_projection.*`、`workflow_explain.*` + 既有文件最小扩展。
- 选择：b。理由：cadence/coverage/probe/projection/explain 是新职责面；塞旧文件会造成 1500+ 行文件继续膨胀且测试定位困难。错误码仍进唯一 taxonomy 表（harness_error.h），检查 ledger 仍唯一在 workflow_analysis——无第二真值。

## D-004 时间解析的独立真值

- 背景：cadence/regularity 需要解析 ISO8601 时间/区间。仓库已有 QDateTime 可用。
- 候选：a) 自写解析；b) QDateTime/QDate + 自写周期语法（`16d`/`P16D`/monthly）。
- 选择：b——datetime 解析用 Qt（平台权威，跨平台/Unicode 稳定），cadence 周期语法用一个小型闭表解析器（known-answer 测试锚定）；两者都允许 unknown 失败。理由：不引入新依赖（envelope 7），Qt 是仓库既有权威；cadence 语法是本 track 新词汇，测试用独立字面真值（手算日期差）锚定，不用被测实现产 oracle。

## D-005 probe 预算语义

- 背景：B 包 bounded probes 需要超时/字节上限。
- 候选：a) 异步线程 + 超时取消；b) 同步 + GDAL 打开前后 stat/budget 检查 + 有界读取（overview/仅 metadata），超时仅在支持的下层的 probe 边界生效。
- 选择：b（同步、预算前置 fail-closed）+ 每类 probe 声明预算常量（irLimits 风格单表）。理由：harness 现有 grounding 全同步（SpatialToolResult 同步返回）；同步+预算失败→typed unknown 与"UNKNOWN 不伪 PASS"纪律一致，避免线程生命周期问题；线程化是更大重构，超出最小 vertical slice，记 follow-up。

## D-006 新错误码命名

- TEMPORAL_CALENDAR_CONFLICT / NUMERIC_DOMAIN_CHAIN / BAND_IDENTITY_MISMATCH / OUTPUT_IDENTITY_MISMATCH —— 沿用既有全大写下划线风格，appenditive 进 `error_codes` 单表 + `errorCategoryForCode`/`retryClassForCode` 映射 + `allErrorCodes()`。语义 category=validation，retry=None。

## D-007 eval corpus 追加而非新建 runner

- 背景：G 包；runner（test_harness_eval_corpus.cpp）已 data-driven 且有 schema 守卫与 ≤400 上限。
- 选择：只追加 `data/agent/evals/cases/**` 数据文件；若需新 category 才扩闭表（先检查现有闭表够用：anti_hallucination/typed_contract/modality_mismatch/invalid_science 已覆盖大部分；temporal/grounding case 归入现有类别 + 工具引用既有 surface）。

## D-008 中文可读 explain 的实现位置

- 背景：F 包要求"中文可读且 bounded"。
- 候选：a) hard-code 中文文案进 explain 模块；b) 文案表（zh-CN 为主）+ 结构化 facts 分离。
- 选择：b。`workflow_explain` 输出 `{summary_zh, causes[], evidence{}}`，summary/causes 为中文模板渲染，结构化数据供 UI/agent 消费；输出 ≤8KiB 截断计数诚实标注。

## D-009 ISSUES.md / open issues 不实施

- 7 条 open issues（#1001-#1007）全部不在本 track ownership（BASELINE.md 逐条表）；ISSUES.md 旧 backlog 中未修条目均为算子内核缺口，非编译器职责。记 OUT_OF_SCOPE，不跨轨修复。

## D-010 测试命名与注册

- 新测试：`tests/test_workflow_facts_11.cpp`、`tests/test_grounding_probes_11.cpp`、`tests/test_workflow_analysis_11.cpp`、`tests/test_workflow_repair_11.cpp`（扩展）、`tests/test_provenance_projection_11.cpp`、`tests/test_workflow_explain_11.cpp`；CMake 注册 append-only 到 tests/CMakeLists.txt（integration commit）。

## D-011 帮助目录不新增 concept 条目

- 背景：`data/help/concepts.json` 有 schema/i18n 测试约束（#959 修复线）。
- 选择：本 track 不动帮助 catalog；agent-facing 文档即工具 schema 的 description 字段 + ADR 0163。理由：最小接线，避免与并行 help 相关修复冲突。

## D-012 pi 行为测试的宿主限制处理

- 背景：`fake_mcp_server.mjs` 靠 shebang 被 spawn，Windows 直接 exec .mjs → `spawn EFTYPE`。master 上现存的 `pi/test/no_drift.test.mjs` 行为段在本机同样失败（pre-existing，非本 diff 引入）。
- 选择：新测试 `scientific_workflow_compiler_11.test.mjs` 用 canary 判定 spawn 能力，失败则 `t.skip("host cannot exec the fake MCP server...")` 并打印原因；静态守护（无 per-tool fork、schema 常量锚、知识页预算）无条件运行。Linux CI 上行为段会正常执行。

## D-013 probe 以独立 SpatialTool 注册而非挂在 grounding_tools 内

- 背景：probe 需要被 agent/corpus 调用（surface 一致）。
- 选择：`grounding_probes.cpp` 自带 `registerGroundingProbeTools()`，在 `spatial_tool.cpp` 注册序列中 `registerGroundingTools()` 之后追加一行；taxonomy overrides 追加两条。理由：不动 grounding_tools 的注册函数体（最小 diff），probe 工具与 probe API 同文件（一个实现）。

## D-014 collection descriptor grounding 放在 planner 而非 entity_resolver

- 背景：compile_workflow 对未预计算 slot 走 spatial:understand；collection descriptor 是 JSON 文档会失败。
- 候选：a) 改 spatial:understand 支持 .json；b) planner 的 grounding 循环里对 understand 失败的 slot 尝试 descriptor grounding。
- 选择：b。理由：a 会改变公共 grounding 工具的语义面（影响所有调用方）；b 只影响编译路径，anti-hallucination 不变（仍走唯一 resolver），失败即无合成。
