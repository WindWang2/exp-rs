# ADR 0172: Scientific Inspector UI — 科学状态与证据检查工作台

Status: accepted · Branch `agent/rs14-scientific-inspector-ui` · Baseline origin/master@4f6632e1f6

## Context

学生和 Agent 需要统一的"科学状态"检查面：数据护照、质量、几何/网格、辐射、时相、预检发现、验证证据。现有 Inspector dock（Workbench 5.0 `InspectorHost`/`InspectorSection`）已有统一宿主与选择上下文（`SelectionContext`），Workbench 7.0 已有 `ProvenanceSection` 只读投影模范。事实源已存在：`AssetSnapshot` 结构、`DerivationRecord`、`DatasetQaReport`、`verifyProductProvenance`、`EvidenceProjector`、`fact_status` 词汇。

## Decision

1. **不新增 dock**。Scientific Inspector 以 7 个新 `InspectorSection`（`sicnu::app::sci`）落进既有 `rsInspectorDock`；provenance tab 继续由既有 ProvenanceSection 承担。避免第二检查面。
2. **view-model 与 widget 分层**。纯 Qt6::Core 的 view-model（`src/app/workbench/scientific/`）产出 versioned `ScientificInspection` value；widget 只呈现。业务事实全部经 provider 接口（`ISci*Facts`）注入；服务缺席 → typed `Unavailable` fallback，禁止 silent fallback。
3. **只投影，不复制**。不新建 provenance/quality/experiment 存储；fact 状态语义映射既有 `fact_status`（derived→inferred 呈现）与 `DiagnosticSeverity`；conflict 为呈现层状态（来源互斥时），不新造权威源。
4. **UI 无隐式修复**。不渲染 repair action；一切修复走既有显式 service 能力（本 track 不新增）。
5. **资源有界**。populate 仅 indexed 查询；每 section ≤32 条并显式截断标注；验证调用每目标 ≤1 次。
6. **双模式呈现**。教学（解释状态含义）/专家（原始 metadata/evidence）来自同一 value，杜绝两套真相。

## Consequences

- 中央文件 delta 最小：`src/app/CMakeLists.txt` 源列表、`tests/CMakeLists.txt` 手写测试块（照 `test_provenance_section` 先例，不加新 CMake lib target）、`main_window_workbench.cpp` 注册块。
- 未来 Agent 工具可直接消费 view-model 的 `toJson()`（`sci_inspection` envelope，`kSciInspectionSchemaVersion="1.0"`）；接线点记录于 `docs/integration.md`。
- 回滚 = 移除注册块 + 删除目录；无 schema 迁移。

## Alternatives considered

- 独立新 dock：制造第二检查面，违背"统一面板"目标，放弃。
- 复用 `sicnu_add_test`：其链接集合固定，为加链接改 900 个测试目标 —— 改手写测试块更小。
- 在 ProvenanceSection 里塞全部科学状态：单一 section 过载、tab 语义混乱、不可按域懒加载。
