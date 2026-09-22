# Slices — RS14-15 Explainable Workflow（TDD 分片）

粒度约定：每片 = RED（失败测试确认因缺能力而红）→ GREEN（最小实现）→ REFACTOR → 窄测试绿 → commit → progress.md 更新。构建纪律：`-j1`，只编本模块与相关窄 target。

## Slice A — StepExplanation schema + fact provenance（sicnu_explain 骨架）
- 建 `src/explain/`（CMake：纯 C++20+jsoncpp 静态库 + layer guard + 根 CMake 一行）。
- `FactProvenance`、`GroundedText`、`EvidenceLink`（kind 闭表 + target 语法）、`SourceReference`、`ParameterRationale`、`StateTransition`、`SkippedStepConsequence`、`ExecutionFacts`、`StepExplanation`。
- canonical `toJson` byte-stable；`fromJson` 严格：版本钉 `exp.step_explanation.v1`、未知键拒绝、SystemFact 缺 machine-kind evidence 拒绝。
- 测试 `test_explain_schema`：往返 byte 相等、未知键红、版本红、SystemFact 伪证红、空 SystemFact 红。
- RED 验证方式：先写测试 + 空 stub，编译过、断言红。

## Slice B — 请求与事实源接口 + 第一个 adapter
- `ExplanationRequest`/`PortFactView` + `state_vocabulary`（闭表 + format 校验）。
- `IOperatorKnowledge/OperatorFacts/ParamFact`、`IAuthoredGuidance`、`IExecutionEvidence/StepEvidence`（纯接口 + 测试用 fake）。
- adapter：`RegistryOperatorKnowledge`（RSOperatorRegistry → OperatorFacts）。
- 测试：`test_explain_request`（轻量：词表/语法/fake）、`test_explain_registry_adapter`（窄重量：真实 registry 解析 rs: 光谱/滤波等 ≥5 算子，参数名来自 live schema）。

## Slice C — authored guidance loader
- `GuidanceStore`：`data/explain/guidance/*.json`、`exp.step_guidance.v1`、安全读（stackLimit）、typed `GuidanceLoadProblem`、fail-closed、role 覆盖、entries 上界。
- 先落 3 个种子 guidance 文件（ndvi / radiometric_calibration / sar_calibrate）驱动测试。
- 测试 `test_explain_guidance_store`：happy、schema 版本、未知键、重复 operator、坏 evidence/引用、深度嵌套拒绝、role 优先、loadProblems 可查询。

## Slice D — builder：parameter rationale / consequence / state composition
- `StepExplanationBuilder`：组装 purpose/prerequisites/assumptions（operator 元数据 SystemFact + authored AuthoredGuidance）、parameterRationale（schema 过滤 + chosenValue 注入）、skipConsequence（authored + Skipped 状态附加执行证据）、stateChanges（PortFactView 合成、mixed/contradiction 显式化）、execution 附加、排序与截断上界、确定性。
- 测试 `test_explain_builder`（fake 接口）：每条语义一 RED→GREEN；两次构建 byte 相等；operator_unknown fail-closed；非算子骨架。

## Slice E — workflow projection adapters（D17 / Engine2 / Lab 请求构造）
- `WorkflowIrProjector`（WorkflowDocument+nodeId → Request）、`Engine2Projector`（StepDef[+StepPlan] → Request）、`ProvenanceFileEvidence`（d17_provenance v1.0 → StepEvidence）。
- 测试 `test_explain_projection`（Qt6::Core + workflow 库窄 target）：三投影字段映射、词表钉扎（vocabulary 与 workflow_ir_v2 语义对齐）、provenance 文件解析（fixture）、坏文件 typed 拒绝。

## Slice F — UI view-model + 教学面板
- `StepExplanationViewModel`（纯逻辑：sections/badges/markdown/确定性）→ 轻量测试 `test_explain_view_model`。
- `StepExplanationPanel`（Qt Widgets 薄壳）+ `ExplainService` + main_window_docks 装配（nodeSelected 接线）。
- offscreen dialog 级冒烟（若已有 dialog test helper 可承受则加，否则靠 view-model + 手动路径测试说明）。

## Slice G — hallucination guard 强化 + coverage oracle + agent tool + e2e
- `ExplanationValidator` 全错误码矩阵测试 `test_explain_validator`。
- authored 全量落盘：≥15 代表算子 guidance（跨 optical/sar/spectral/temporal/change/classify/terrain/io 家族）。
- coverage oracle `test_explain_guidance_coverage`（live registry：operatorId 解析、参数名存在、evidence 语法、数量下限）。
- `explain:step` SpatialTool（单算子模式 + 文档模式 + fail-closed）+ `test_explain_agent_tool`。
- e2e 教学场景 `test_explain_e2e_teaching`（DN→定标→NDVI：投影→解释→校验→markdown→断言四类信息可见）。
- `docs/adr/0167-step-explanations.md`、`docs/explain/step-explanations.md`、`docs/integration.md`、guidance README + JSON Schema。

## Review gate 后收尾
- 两轮 review（adversarial-reviewer subagent）→ 修复 P0/P1/P2 → fetch origin → 动态去重/union → 窄回归 → PR。
