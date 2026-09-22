# Recon — RS14-15 Explainable Scientific Workflow / Why-this-step

- Date: 2026-09-21
- Baseline HEAD: `4f6632e1f6bb41f90729800d0c7bf569ff34edb3`（= prompt 编写基线，`git fetch` 后 origin/master 无前移）
- Branch: `agent/rs14-explainable-workflow`，worktree `/home/kevin/projects/rs-studio/exp-rs-wt-rs14-explainable-workflow`
- Dynamic dedup: 无 open PR（gh 实查）；open issues 与本 prompt 回避清单一致（#1146–#1187），无新增可吞并项。
- 主仓库 master 上存在未提交修改 `tests/test_env_doctor.cpp`（他人遗留），本 track 不触碰。

## 1. 已有能力（必须复用，不得复制）

### 1.1 每算子静态知识（系统事实的来源）
- `RSOperator::metadata()`：`src/operators/framework/rs_operator.h:110`、`rs_operator.cpp:27-40` —— 已有 `purpose / useCases / prerequisites / limitations / workflowHints / tags`。
- 参数 schema：`rs_schema.h:21-82`（name/type/description/default/enum/range）。
- GUI/agent 镜像：`AlgorithmDescriptor::AgentMetadata`（`src/processing/framework/algorithm_descriptor.h:63-132`）。
- 帮助系统：`AlgorithmPage.assumptions`、`ParameterKnowledge(meaning/recommended/tradeOff/dependsOn)`（`src/help/help_descriptor.h:69-104`），加载自 `data/help/operators/*.json`（`HelpContentStore`，"C++ contains no help prose"，ADR 0146 禁 fallback）。
- 科学契约：`ScientificContract`（`src/contracts/scientific_contract.h:42-96`）—— input/outputDomain、noDataPolicy、refusalCodes、evidence anchor、`"exp.scientific_contract.v1"`。
- 能力知识层：`data/agent/capabilities/*.json`（D8/ADR 0154），`CapabilityKnowledge::validateEntry` 未知键拒绝 + drift 测试（`tests/test_capability_drift.cpp`）。**#1151 红灯区，不碰。**

### 1.2 三套 workflow 表示（builder 都要能投影）
- Workflow Engine 2.0：`StepDef/WorkflowDefinition`（`src/workflow/workflow_types.h:33-53`）、执行 `StepPlan`（`workflow_run.h:53-101`，status 含 Skipped、resolvedParams、artifact digest）。
- D17 IR 2.0：`NodeFact/PortFact`（`src/workflow/workflow_ir_v2.h:56-125`，**PortFact.radiometricState = "DN|Radiance|TOA|BOA|Index|Mask|*|None"**）、`WorkflowDocument` v2.0/2.1、`PipelineRunCoordinator`、`NodeStatusSnapshot`（含 Skipped 级联、artifact fingerprint、cacheHit、lineageSignature）。
- Agent IR 1.0：`IrNode.semanticOutput / source`（`src/agent/harness/workflow_ir.h:121-133`）。
- Lab：`LabSpec steps[]/principles[].refs`（`data/labs/labspec.schema.json:70-127`），`LabSpec` 代码 `src/app/widgets/lab_spec_loader.h`。

### 1.3 Provenance / 执行事实（evidence links 的来源）
- D17 per-run provenance 图：`src/workflow/workflow_provenance.h:37-90`，`provenance_<runId>.json`，`kind "d17_provenance", version "1.0"`。
- 资产级 `DerivationRecord`（权威）：`src/data/derivation_record.h:94-143`，**已带 `workflowId/workflowRunId/stepId`**。
- Experiment：`ExperimentRun`（identity pins、artifacts、`executionRef`）、`EvidenceProjector`（v1）、`LineageGraph`（ADR 0138）。
- 每算子 trail：`RSOperationLogger::OperationRecord`（`rs_operation_logger.h:18-28`）。

### 1.4 最接近的先例（纪律来源）
- `sicnu::agent::harness::explain::explainDecisionChain`（`src/agent/harness/workflow_explain.h`）：run 级失败 traceback，fact grounding 分类 `observed/declared/derived/unknown`，有界（ExplainLimits）。**它是"失败解释"，不是 per-step 教学解释；本 track 不修改它，只复用其 grounding 纪律。**
- `harness:explain` 工具（`src/agent/harness/plan_tools.cpp:406`）：run 级 Run Explanation。

### 1.5 工程 pattern（照抄，不发明新制度）
- Authored data 模式：JSON 单一事实源 + JSON Schema + typed loader error + 禁 fallback（LabSpec / HelpContentStore）。
- 幻觉防护模式：未知键拒绝、id 必须在 live registry 解析、闭表词表 + drift 测试（`CapabilityKnowledge::validateEntry` + `test_capability_drift.cpp`）。
- jsoncpp 安全读：`stackLimit=128` + `Json::Exception` typed catch + isObject 检查（`src/sdk/exprs/plugin_manifest.cpp:858-905`）。**无共享 helper，本模块自带一个私有 helper。**
- 新模块 pattern：`src/<module>/CMakeLists.txt` 静态库 + `Sicnu::` alias + layer guard foreach（`src/help/CMakeLists.txt`），根 CMakeLists 仅加一行 `add_subdirectory`（`CMakeLists.txt:740-809` 块）；测试统一在 `tests/CMakeLists.txt` 注册（`sicnu_add_io_test` 轻量无 Qt）。
- Typed result：核心层用 `sicnu::workflow::Result<T>` 语义（自建同形 value type），agent 层有 `HarnessError` 闭表。
- UI 钩子：`PipelineCanvasWidget::nodeSelected(nodeId)`（`src/app/pipeline/pipeline_canvas_widget.h:37`）+ `Ir2PipelineDesignerDock::currentDocument()` → `NodeFact`；dock 装配点 `src/app/main_window_docks.cpp`。

## 2. 缺口（= 本 track 交付物）
1. **无 per-workflow-step 解释对象**：purpose/prerequisites 只在算子级静态存在，没有任何东西把 算子元数据 + authored guidance + 实例参数 + PortFact 前后状态 + 执行证据 合成为"这一步为什么存在/改了什么"。
2. **无实例级 parameter rationale**（"为什么这一步用 8×8 窗口"只有 generic parameter meaning）。
3. **无 skipped-step consequence**（Skipped 状态存在，但无"跳过意味着什么"）。
4. **无 evidence links 组装**（数据都在：DerivationRecord.stepId、provenance json、ExperimentRun，但没有 per-step 组装器）。
5. **无 operator 级 source references/citations**（只有 LabSpec principles.refs 与光谱表 citation）。
6. **无解释质量校验器**（引用不存在的 parameter/state 必须可拒绝）。
7. **无 machine-readable per-step 解释接口**（`harness:explain` 只覆盖 run 级失败）。
8. **无 teaching UI 的 why/how/what-changed 面板**。

## 3. 不做什么（scope 边界）
- 不改 capability mirror / `data/agent/capabilities`（#1151）；不做 contract projection（#1187）。
- 不修改 `workflow_explain.h` / `harness:explain` 现有语义。
- 不新建第二套 provenance/experiment/registry —— 只通过 adapter 读既有事实源。
- 不实现"自动修正科学状态"——只解释与引用，一切状态判定仍由 radiometric FSM / SAR guard 负责。
- 不在解释层引入网络依赖（离线可用）；不复制 GUI 业务逻辑进 core。
- 不处理回避清单中任何 open issue（发现即记录 observed，不修）。

## 4. 风险
- R1 三套 workflow 表示投影时语义漂移 → 用同一 `ExplanationRequest` 归一化 + per-表示 adapter 测试。
- R2 `PortFact.radiometricState` 词表在 workflow 模块内是字符串字面量 → 本模块定义同闭表 + 一条 integration drift 测试钉住（防双真相源漂移）。
- R3 全量构建成本 → 核心库设计为纯 C++20 + jsoncpp（无 Qt/QGIS 链接），轻量测试 target；adapter/UI 测试窄目标。
- R4 与并行 inspector-ui track（agent/rs14-scientific-inspector-ui，当前与 master 零 diff）重叠 → 边界：本 track 交付 builder + validator + machine 接口 + 最小示例面板；对方未来 UI 应消费同一 builder（写入 docs/integration.md 与 PR 描述）。
- R5 authored guidance 与算子演化漂移 → coverage 测试 + 参数名/状态词表校验（fail-closed，drift 即红）。

## 5. 与其他 tracks 的接口
- 提供：`StepExplanation` value object（versioned `exp.step_explanation.v1`）、`IStepExplanationBuilder`、authored guidance store、`StepExplanationViewModel`（纯逻辑）、JSON wire（agent/MCP 可直接消费）。
- 消费：`RSOperatorRegistry`（事实）、`WorkflowIR 2.0`/Engine2.0/LabSpec（投影源）、`DerivationRecord`/`ProvenanceGraph`/`NodeStatusSnapshot`/`StepPlan`（evidence）、`PortFact` 状态词表。
- 需要但可能尚不存在的类型：无 —— 全部事实源在 master 已存在；无需 fake provider 之外的等待。

## 6. 与 open issues 去重矩阵（摘要）
| 区域 | 关系 |
|---|---|
| #1151/#1187 capability mirror / contract projection | 明确避开；解释层从 operator registry/descriptor 直读，不经 mirror |
| #1158/#1152 workflow cancellation | 不触碰执行路径，只读 NodeStatusSnapshot/StepPlan |
| #1146/#1147/#1164/#1165 SAR 科学正确性 | 不修改 SAR 算子；状态词表仅作解释引用 |
| #1177 observatory baseline | 无关（不做性能基线） |
| #1179 test oracle potency | 反向约束：本 track 测试必须真红真绿 |
| 其余清单项 | 均不在解释层路径上；只读旁观 |
