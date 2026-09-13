# ARCHITECTURE — 10.0 增量设计决策

既有权威边界（全部维持，不新增平行实现）：

```
Pi (唯一 agent runtime)
WorkflowRunCoordinator → TaskCenter → JobEngine → Executor/Operator   (唯一调度链)
QGIS (唯一地图/布局渲染权威)   MapSpecCompiler (唯一 MapSpec⇄QgsPrintLayout 桥)
composition solver / quality preflight / export (唯一制图引擎, src/agent/cartography)
src/geospatial (唯一 I/O 权威)   Dataset/Experiment stores (元数据权威)
WorkbenchHost / SelectionContext / CommandRegistry / WorkbenchStateModel (唯一 shell 权威)
QgisDisplayManager (唯一 view/layer 展示权威)
```

## D10-1 — WorkbenchObjectRef：稳定类型化对象身份（WP-A）

新增 `src/app/workbench/object_identity.h`：

```cpp
enum class ObjectKind { Asset, Layer, Result, Experiment, Dataset, Model, WorkflowRun, MapLayout };
struct WorkbenchObjectRef {
  ObjectKind kind;
  QString id;          // 各权威 store 的既有 id（不新造 id 空间）
  QString displayName; // 投影缓存（可空）
};
```

- id 来源逐 kind 对齐权威：Asset→`data::AssetId`；Layer→`DisplayLayerId`；Result→
  governance entity id；Experiment→ExperimentStore run id；Dataset→DatasetStore id；
  Model→ModelCatalog name；WorkflowRun→WorkflowRunCoordinator run id；MapLayout→layout name。
- **只有一个 primary selection**：`SelectionContextSnapshot` 增 `primaryObject`（首个
  非层选择优先，层选择次之——规则纯函数可测）；related selection 保持既有列表语义。
- SelectionContext 增 push 式通知：`notifyExperimentSelection` / `notifyDatasetSelection` /
  `notifyModelSelection` / `notifyWorkflowSelection`（照 `notifyAssetSelection` 既有模式，
  面板信号 → context 订阅，无跨面板 widget 访问）。
- ContextFacts 增 `hasExperiment/hasDataset/hasModel/hasWorkflowRun`；ContextRules 补
  unavailabilityReason 分支。**不**改既有字段语义（wb5-9 测试不动）。
- provenance links：新增纯函数 `objectLinks(snapshot, services)` 复用 ProvenanceSection
  的同一解析链（asset→entity→layer source→experiment lineage），供 inspector、agent
  投影、VA linked brushing 三方消费；ProvenanceSection 改为调用同一 helper（收敛，不重复）。

## D10-2 — Agent context 投影（WP-A / H）

- `src/agent/mcp_server.h` 增只读 context-provider seam（`std::function<Json::Value()>`），
  GUI 装配点注入：current workbench id、primaryObject、对象列表、ContextFacts、
  可用命令 id 列表（registry 快照）。
- 新 MCP 工具 `workbench:context`（read-only，无副作用）：返回上述 JSON。agent 因此
  能读稳定 selection/context；**写入仍全部经既有 agent 工具与命令权威**。
- 方向 2（agent→UI）已有：plan→WorkflowDefinition→canvas（test_agent_canvas_sync）；
  本 track 补 UI→agent 与 agent 地图/排版修改后的 UI 刷新（经 QgisDisplayManager/
  layout 重载信号，天然成立，补集成测试）。

## D10-3 — Cartography workflow bridge（WP-B / G，关闭 C-1）

分层（不复制引擎）：

1. **引擎层（复用）**：`composition`（solve）、`quality`（preflight）、`MapSpecCompiler`
   （compile/extract）、`export`（原子导出）、`repairMapSpecWithLedger`。零改动或仅窄改。
2. **operator 适配层（新）**：`src/operators/cartography/cartography_operators.*` ——
   `cartography:compose` / `cartography:preflight` / `cartography:validate` /
   `cartography:repair` / `cartography:export` 五个薄 RSOperator：
   - 输入 schema 与 `cartography:*` agent 工具的输入契约逐字对齐（同名字段）；
   - compose/export 需要 QgsProject/QgsApplication → 构造时探测 QGIS host
     （`qgisAppFacade` seam），无 host 时类型化拒绝（`E_CARTO_NO_QGIS_HOST`，
     诚实错误码，不静默）；preflight/validate 纯 JSON 可在 headless 跑；
   - 注册进 `RSOperatorRegistry`（`rs_operators_init.cpp` append-only 一行 + 独立
     integration commit）→ workflow node / TaskCenter / CLI pipeline / agent 全部可达。
3. **GUI 层（新）**：`src/app/cartography/cartography_dock.*`：
   - 输入：统一 selection（primaryObject=Layer/Result → 地图图层；asset 路径直填）；
   - 模板目录：`data/cartography/templates`（既有权威，`cartography:list_templates`
     同源读取）；
   - 动作经 CommandRegistry：`cartography.compose/preflight/repair/export/preview`；
   - live preview：`MapSpecCompiler::compile` → QgsPrintLayout → bounded pixmap 渲染
     （QPainter，尺寸上限 + offscreen 安全）；不复制 compiler；
   - preflight 报告面板 + repair ledger 展示（复用 quality/repair 输出 JSON，
     RsResultSummary 风格）；
   - export evidence：sha256 + 页面清单（复用 export 引擎输出）。
4. **workflow preset**：`cartography:compose`→`cartography:preflight`→`cartography:export`
   链式 preset（mapspec JSON 经占位符语法消费上游 `rs:*` 输出路径）→「分析结果作为
   MapSpec input」的平台化路径。

## D10-4 — Visual Analytics 平台（WP-C）

新增 `src/app/visualanalytics/`：

```cpp
// va_data.h — 纯值类型（Qt 基础类型，无 layer/GDAL 依赖）
struct VaHistogram   { QVector<double> binEdges; QVector<qint64> counts; ... };
struct VaSeries      { QVector<double> x, y; QString xLabel, yLabel; };
struct VaScatter     { QVector<QPointF> points; QVector<int> classOf; ... };
struct VaBoxPlot     { QVector<double> q0..q4, outliers; ... };
struct VaCategoryMatrix { QStringList rows, cols; QVector<qint64> cells; } // 混淆/转移
struct VaClassAreas  { QStringList classes; QVector<qint64> pixels; }
```

- `va_source.h`：异步供给契约——`VaSourceJob` 在 `RsScanPool`（唯一 sanctioned UI 扫描池）
  上计算上述值类型，`marshalTo`（`marshal_ui.h`）回 GUI；每源有界采样上限
  （直方图 ≤ 4M 样本步进采样、scatter ≤ 20k 点、profile 按波段数），generation token
  取消；UI 状态机 empty/loading/error/ready。
- `va_chart_*.cpp`：QPainter 自绘 chart hosts（bar/line/scatter/box/matrix/area），
  颜色取 `SicnuUi::Tokens`，无硬编码 QColor（pipeline 场景例外不适用）；accessibleName、
  空态用 RsEmptyStateWidget 契约、导出 CSV/JSON（有界）。
- `va_selection_hub.h`：linked brushing——chart 选中的 bin/点/类 → 空间/属性过滤谓词 →
  地图高亮或 VA 图表互滤；经 SelectionContext/ObjectRef，不直改他人 widget。
- 消费者（Phase 3+）：workspace browser Result 行「可视化」→ 混淆矩阵/转移矩阵/类面积/
  指标图；ROI 统计 → 箱线/散点；光谱剖面收敛为 VaSeries 消费方。
  **既有 HistogramWidget 保持不动**（收敛建立在新消费者侧，不做大爆炸迁移）。

## D10-5 — Workflow 可视化编辑 2.0（WP-D）

- 节点 IO 徽标：`PipelineNodeItem` 增端口类型标注——来源 `RSOperatorRegistry` schema 的
  端口 `x-ui-type`（raster/vector/table/…）；`validatePortConnection` 升级为类型兼容矩阵
  （src/workflow 窄缝：只增不改语义）。
- 参数 inspector：编辑器 dock 右侧 `PipelineParameterInspector`——选中节点 →
  `SchemaFormBuilder` 表单（复用 WorkbenchEnumProvider seam）；编辑写回
  `WorkflowDefinition.steps[].params`，`workflowChanged` 单信号通知。
- preflight 投影：编辑器「检查」动作跑既有 `workflow_preflight` 逻辑（agent 工具同源），
  错误映射到节点 error badge + 修复建议文本；不建第二套 preflight。
- run/cancel/resume 沿用 WorkflowSessionController/WorkflowRunCoordinator（无新引擎）。

## D10-6 — 多视图联动（WP-E）

- 新 `src/app/shell/view_link_controller.*`：管理 N 个 `DisplayViewId` 的
  extent/cursor/visibility 联动组；extent 用 center+scale 对齐（CRS 不同视图拒绝联动并
  提示，诚实）；cursor 投影坐标换算；visibility 经各视图 layer tree 权威同步。
- 与 `rs_dual_viewport_sync_controller` 的关系：新控制器注册为 link 组管理者，dual
  viewport 场景收敛到同一实现（保留原类作为兼容装配，行为不回退）。
- 安全：所有画布操作遵守 `stopRenderingAndSettle` 与 `viewAboutToBeRemoved` detach 契约。

## D10-7 — Processing UX 收敛（WP-F，有界）

- TaskPanelHost 算子选择器增：搜索（名称/关键词）、最近使用 + 收藏（QSettings，
  `workbench/processing/recent|favorites`）、capability 过滤（消费 #950 capability
  knowledge 只读 API）；live availability 沿用 ContextRules。
- 不迁移 72 个 dialog 为 modal 之外的入口（兼容旧工作流）；只保证新目录入口可达全部
  `rs:` 算子与 toolbox 算法。

## 决策记录

- D-10.0-1: ObjectRef 不新造 id 空间——复用各 store 既有 id，避免第二权威。
- D-10.0-2: cartography 进 workflow 走 RSOperator 适配层（薄、同 schema），不改
  agent 工具、不改引擎；GUI dock 与 workflow node 共用同一适配层。
- D-10.0-3: VA 平台只收新消费者；既有 widgets 不大爆炸迁移（surgical）。
- D-10.0-4: N 视图联动是 display manager 之上的组合服务，不是新 view 权威。
- D-10.0-5: agent context 投影为只读 seam；不增加 agent 写路径。
