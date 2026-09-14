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
- **只有一个 primary selection**：`ContextRules::primaryObject(snapshot)` 纯函数
  （WorkflowRun > ExperimentRun > Dataset > Model > Result > Asset > Layer，首个非空
  列表/活动层胜出）；related selection 保持既有列表语义。快照本身仍是哑值，无
  primaryObject 字段。
- SelectionContext 增 push 式通知：`notifyExperimentSelection` / `notifyDatasetSelection` /
  `notifyModelSelection` / `notifyWorkflowSelection`（照 `notifyAssetSelection` 既有模式，
  面板信号 → context 订阅，无跨面板 widget 访问）。
- ContextFacts 增 `hasExperiment/hasDataset/hasModel/hasWorkflowRun`；ContextRules 补
  unavailabilityReason 分支。**不**改既有字段语义（wb5-9 测试不动）。
- provenance links：`resolveSelectionAssetTargets(snapshot, dm, ws)` 复用 ProvenanceSection
  的同一解析链（asset→entity→layer source，first-non-empty 级联，kObjectLinkMaxTargets=4），
  供 inspector、agent 投影消费；ProvenanceSection 改为调用同一 helper（收敛，不重复）。
  Experiment lineage 的结构化 objectLinks 投影记 follow-up。

## D10-2 — Agent context 投影（WP-A / H）

- seam 实现为 `WorkbenchContextTool`（`src/app/workbench/agent_context_tool.h`，实现
  SpatialTool 接口）：GUI 装配点经 QPointer 守卫的 provider 注入——current workbench id、
  primaryObject、对象列表、ContextFacts、已注册命令 id 词汇表（registry 快照，非可用性
  保证）。`mcp_server.cpp` 仅增 `workbench:` 前缀路由（kAllowed + tools/call），无服务端 seam。
- MCP 工具 `workbench:context`（read-only，无副作用）：返回 `workbenchContextToJson` JSON。
  agent 因此能读稳定 selection/context；**写入仍全部经既有 agent 工具与命令权威**。
- 方向 2（agent→UI）已有：plan→WorkflowDefinition→canvas（test_agent_canvas_sync）；
  本 track 补 UI→agent 与 agent 地图/排版修改后的 UI 刷新（经 QgisDisplayManager/
  layout 重载信号，天然成立，补集成测试）。

## D10-3 — Cartography workflow bridge（WP-B / G，关闭 C-1）

分层（不复制引擎）：

1. **引擎层（复用）**：`composition`（solve）、`quality`（preflight）、`MapSpecCompiler`
   （compile/extract）、`export`（原子导出）、`repairMapSpecWithLedger`。零改动或仅窄改。
2. **operator 适配层（新）**：`src/agent/cartography/cartography_operators.*`（sicnu_agent
   已 PUBLIC 链接 sicnu_operators——零循环依赖；不触碰 `rs_operators_init.cpp`）——
   `cartography:compose` / `cartography:preflight` / `cartography:validate` /
   `cartography:repair` / `cartography:export` 五个薄 RSOperator：
   - 输入/输出 schema 与行为同 `cartography:*` agent 工具逐字对齐（同名字段、
     repair 有界循环与台账键、export pages 回显、resolve→preflight 顺序）；
   - compose/export 需要 QgsApplication → `requireQgisHost()` 探测（qobject_cast
     QgsApplication），无 host 时以 `ErrorCode::NotInitialized` 类型化拒绝，不静默；
     preflight/validate/repair 纯 JSON 可 headless 跑；
   - 注册：`initCartographyOperators()`（幂等）由 app main 与 CLI main 显式调用 →
     workflow node / TaskCenter / CLI pipeline / agent 全部可达。
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

- `va_source.h`：异步供给契约——`VaDataSource` 在 `RsScanPool`（唯一 sanctioned UI
  扫描池）上计算上述值类型，marshal（`marshal_ui.h` + QPointer 守卫）回 GUI；
  上限由生产者声明并执行（本 track 面板：512² 直方图/剖面缩略图、256² scatter、
  ≤4096 散点），generation token 取消；UI 状态机 empty/loading/error/ready。
- `va_chart_widget.cpp`：QPainter 自绘 chart host（histogram/line/scatter/box/matrix/
  area），颜色取 `SicnuUi::Tokens`（双主题各自 token 集合），无硬编码 QColor；
  绘制侧容量封顶（points/bars/matrix cells/outliers）；accessibleName、手绘空/载/错态、
  导出 CSV/JSON（仅 Ready 态，载荷本身有界）。
- linked brushing（As-Built）：在 `VaWorkbenchPanel` 内以真实消费者落地——直方图
  拖选 x 范围 → 散点在既有有界载荷上客户端互滤（不重扫）。进程级 selection hub
  抽象记 follow-up（单消费者时不建）。
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

## D10-6 — 多视图联动（WP-E，As-Built）

- `src/app/shell/view_link_controller.*`：N 个 `DisplayViewId` 的 linked extent 组——
  per-view 开关、16ms 节流的 extent 扇出（跨 CRS 经 QgsCoordinateTransform 变换；
  未实现 cursor/visibility 同步，记 follow-up）。本阶段未在 shell 装配
  （无 `view.*` 命令）——类与测试交付，装配点即 QgisDisplayManager 的视图注册方。
- 与 `rs_dual_viewport_sync_controller` 的关系：dual viewport（1x2 分割主画布）保留
  专用控制器，本控制器面向 Display Manager 注册视图，无重叠面。
- 安全：`viewAboutToBeRemoved` detach；传播循环内 mApplying 重入门禁。

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
