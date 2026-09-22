# Plan — RS14-15 Explainable Scientific Workflow / Why-this-step

> 架构决策记录见 `docs/adr/0167-step-explanations.md`（实现时落盘）。本计划经 recon 自审后冻结。

## 1. Problem statement

平台已有每算子的静态知识（purpose/prerequisites/assumptions/参数含义）和完整的执行事实（PortFact、DerivationRecord、provenance 图、StepPlan/NodeStatusSnapshot），但**没有任何一层把它们合成为"单个流程步骤"的可解释对象**。本科生点击设计器节点看不到"为什么做/改了什么/跳过会怎样"；Agent 拿不到 machine-readable 的 per-step 解释；并且没有任何机制防止解释内容引用不存在的参数或状态（AI 幻觉当事实）。

## 2. User stories

### 本科生实验视角
- 在 D17 设计器点击"辐射定标"节点 → 面板展开：purpose（为什么做）、prerequisites（为什么必须在 NDVI 之前）、state before/after（DN → TOA）、参数 rationale（为什么用这个值/配错会怎样）、skip consequence（跳过的科学后果）、证据（operator schema、科学契约、教材引用）。
- 每条内容带徽章：**系统事实**（机器可验证）/ **编写指引**（教师撰写）/ **推断**（由引用的事实合成）——学生能区分"机器说的"和"老师写的"。
- 引擎把某步标成 Skipped 时，面板解释跳过的原因与对下游的影响。

### AI Agent 视角
- 通过工具 `explain:step` 在执行前获取某 step 的 versioned JSON 解释（purpose/prerequisites/assumptions/parameter rationale/skip consequence/evidence links）。
- 所有字段带 provenance；authored guidance 永远不会被标成系统事实；未知算子/未知参数名返回 typed error，fail-closed。
- 同一 builder 同时服务 workflow（D17 文档）、plan（Engine 2.0 StepDef）、lab stage（LabSpec 步骤）三种投影——Agent 无需知道三套表示的差异。

## 3. Architecture

```
┌ UI: StepExplanationPanel (src/explain/ui, Qt Widgets, 零业务逻辑)
│     ← PipelineCanvasWidget::nodeSelected → ExplainService
├ Agent: explain:step SpatialTool (src/agent/spatial_tools, 注册一行)
│     → 返回 exp.step_explanation.v1 JSON
├ Adapters: sicnu_explain_adapters (Qt6::Core)
│     RegistryOperatorKnowledge (RSOperatorRegistry)
│     WorkflowIrProjector / Engine2Projector (NodeFact/PortFact/StepDef → Request)
│     ProvenanceFileEvidence (provenance_<runId>.json v1.0)
│     ExplainService (组装：guidance 目录 + 全部 adapter)
└ Core: sicnu_explain (纯 C++20 + jsoncpp，无 Qt/QGIS，layer guard)
      FactProvenance{SystemFact, AuthoredGuidance, InferredExplanation}
      StepExplanation value objects + canonical JSON (byte-stable)
      ExplanationRequest (三套 workflow 表示的归一化投影目标)
      IOperatorKnowledge / IAuthoredGuidance / IExecutionEvidence (稳定接口)
      GuidanceStore (data/explain/guidance/*.json, exp.step_guidance.v1, fail-closed)
      StepExplanationBuilder (组合 + provenance 戳记 + 冲突显式化)
      ExplanationValidator (幻觉防护)
      StepExplanationViewModel (纯逻辑, badges + markdown)
```

单一事实源原则：核心层不复制任何算子/契约/状态事实；authored guidance 只是**教学叙事层**，通过 validator 钉在 live schema/registry 上。

### 稳定接口（供其他 track 消费，见 docs/integration.md）
- `sicnu::explain::StepExplanation` + `toJson()` —— versioned wire format `exp.step_explanation.v1`。
- `sicnu::explain::StepExplanationBuilder(IOperatorKnowledge&, IAuthoredGuidance&, IExecutionEvidence*)` —— 三接口均可 fake；并行 track（如 inspector-ui）只需实现/复用 adapter。
- `sicnu::explain::StepExplanationViewModel` —— UI 无需理解 schema。

## 4. Public API / data schema（要点）

### FactProvenance（信任分类，全 schema 贯穿）
- `SystemFact`：机器可验证，只能由 adapter/builder 产生；**必须**携带 machine-kind EvidenceLink（operator_schema|workflow_document|derivation|provenance|operation_log|contract）。
- `AuthoredGuidance`：教师/专家撰写，来自 `data/explain/guidance/`，永不可标 SystemFact。
- `InferredExplanation`：由被引用事实合成的叙事，必须携带 ≥1 条 EvidenceLink 指向其 grounding。

### StepExplanation（`exp.step_explanation.v1`）
workflowIdentity(workflowKind d17_designer|guided_pipeline|lab|agent_plan, workflowId, stepId, stepTitle, stepKind)、operatorId/operatorDisplayName、purpose[]、prerequisites[]、scientificAssumptions[]（均 `GroundedText{text, provenance, evidence[]}`）、stateChanges[]（aspect/radiometric_state|crs|resolution|band_count, before, after, explanation, provenance）、parameterRationale[]（parameter, chosenValue, rationale, misconfigurationConsequence, provenance）、skipConsequence?（summary, detail, downstreamRoles[]）、execution?（status/elapsedMs/cacheHit/artifact digest/时间戳, SystemFact）、evidenceLinks[]、sourceReferences[]（title/kind textbook|paper|standard|doc/locator）、trustNotes[]。JSON 往返 byte-stable；`fromJson` 严格（版本钉死、未知键拒绝、SystemFact 无机器证据即拒绝）。

### ExplanationRequest（投影归一化）
workflowKind/workflowId/stepId/stepTitle/stepKind/operatorId/parameters(Json::Value)/inputPorts[]/outputPorts[]（PortFactView{portName, dataType, stateToken, crs, resolutionX/Y, bandCount}）/runId?/executionStatus?。stateToken 属闭表词表（与 PortFact.radiometricState 同集，integration 测试钉住）。

### Authored guidance（`exp.step_guidance.v1`，`data/explain/guidance/<operator-slug>.json`）
operatorId、role?（角色覆盖）、purpose、whenToUse、prerequisitesNote[]、assumptions[]、parameterRationale[]{parameter, rationale, misconfigurationConsequence}、stateNarrative?{before, after}（教学期望值，与 port facts 冲突时**显式报告 contradiction**，不静默合并）、skipConsequence{summary, detail, downstreamRoles[]}、references[]{title, kind, locator}、teachingNote?。配套 JSON Schema `data/schemas/step_guidance.schema.json`（draft-07）。

### Builder 语义（fail-closed）
- operatorId 无法解析 → typed failure `operator_unknown`（非算子步骤 stepKind≠operator → 骨架解释 + trustNote，不冒充算子事实）。
- authored 引用了 schema 中不存在的参数 → 记入 `BuildOutcome.problems`（不进产物），validator 同样报 error。
- stateChanges 仅从 PortFactView 合成；ports 缺失 → 不生成条目（缺席≠编造）；输入端口间不一致 → "mixed" + trustNote。
- authored stateNarrative 与 port facts 矛盾 → `state_contradiction` problem + trustNote（显式化，不裁决谁对）。
- executionStatus==Skipped → skipConsequence 附执行证据；authored 永远附带（前瞻教学）。
- 所有列表稳定排序，`toJson` 两次构建 byte 相等（确定性测试）。
- 资源上界：guidance 单文件 ≤256 entries、全库 ≤1024；输出 JSON ≤64KiB 超限截断并记 trustNote。

### Validator（幻觉防护，error 级）
`unknown_parameter_reference` / `unknown_operator` / `unknown_state_token` / `invalid_evidence_link` / `system_fact_without_machine_evidence` / `schema_version_unsupported` / `empty_system_fact` / `inferred_without_evidence`。Loader 层另有结构错误码（unknown_key/missing_field/duplicate_operator/…），语义校验（参数名、算子存在性）针对 live `IOperatorKnowledge` 执行。

## 5. Migration / compatibility
纯增量：新目录 `src/explain/`、新数据 `data/explain/guidance/`、新 schema 文件、根 CMake 一行 `add_subdirectory`、tests 若干行、`src/agent/spatial_tools` 增 1 文件 + 1 注册行、`src/app/main_window_docks.cpp` 增 dock 装配（约 30 行）。不修改任何现有行为路径；kill-switch = 移除上述挂载点。

## 6. Observability
- `GuidanceStore::loadProblems()`（结构与语义问题，fail-closed skip-and-record）。
- `BuildOutcome{explanation, problems[]}`（builder 拒绝/忽略的内容全部可见）。
- 工具层 typed errorCode（`explain:operator_unknown` 等，SpatialToolResult envelope）。
- UI trustNotes 直接展示。

## 7. Security / trust boundary
- 离线：核心与测试零网络；references 是文献指针（locator 文本），从不 fetch。
- JSON 输入一律 stackLimit=128 安全读（jsoncpp depth-bomb 纪律）；工具输入校验 operatorId 格式。
- 信任不可伪造：手工构造的 `exp.step_explanation.v1` JSON 中 SystemFact 字段缺机器证据 → fromJson/validator 拒绝。
- 解释层只读：绝不写科学状态、绝不自动修正（radiometric FSM/SAR guard 仍是唯一裁决者）。

## 8. Performance budget
- GuidanceStore 一次性加载 ≤1024 entries（预期 ~20 文件）。
- build() 单步 O(算子元数据 + 该步 authored 条目)，无 IO 热路径；provenance 文件按需解析且有 entry 上限。
- 面板渲染走 ViewModel 预分组文本，节点点击无重算（builder 结果可按 (workflowFingerprint, nodeId) 缓存于调用方，v1 不缓存即可满足交互预算 <10ms 量级）。

## 9. Test strategy
- 轻量 Catch2（无 Qt）：schema 往返/严格解析/确定性；guidance loader happy/sad/深度炸弹；builder 语义（fake 三接口）；validator 幻觉矩阵；view-model。
- 重量窄目标（Qt6::Core + workflow）：WorkflowIR/Engine2/LabSpec 投影映射 + 状态词表钉扎。
- 覆盖率 oracle（live RegistryOperatorKnowledge）：每个 guidance 文件的 operatorId 必须在 live registry 解析、parameterRationale 参数名必须存在于 live schema、≥15 个代表算子、evidence 语法合法 —— 这就是反漂移红灯。
- 端到端教学场景测试：DN→定标→NDVI 两步文档，投影→解释→校验→markdown，断言学生可见四类信息。
- Agent 工具测试：单算子模式 / 文档模式 / 未知算子 fail-closed。
- 所有测试真红真绿：每个行为测试先以"缺新能力"验证 RED（TDD 分片见 slices.md）。

## 10. Work packages（= slices.md）
A schema+provenance → B adapters → C guidance loader → D rationale/consequence 语义 → E workflow projection → F UI view-model + 面板 → G hallucination guard 强化 + coverage oracle + agent tool + e2e。

## 11. Rollback / kill-switch
删除根 CMakeLists 一行 + main_window 装配块 + 工具注册行即整体下线；无数据迁移、无持久化格式变更。

## 12. Definition of Done
公共 DoD + track DoD（teaching e2e、machine-readable 接口、单一事实源、typed failures、离线、资源上界、动态去重）；见 PR 模板清单。
