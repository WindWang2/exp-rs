# CAPABILITY MATRIX — master(7d78059d1a) 现状 vs 10.0 计划新增

图例：✔ 有实现+生产调用方+测试；◐ 部分实现（注明缺口）；✘ 无。

## A. UI architecture（audit 结论）

| 组件 | 状态 | 缺口 → WP |
|---|---|---|
| WorkbenchHost/IWorkbench | ✔ | — |
| WorkbenchStateModel | ✔ (wb9 M1) | — |
| SelectionContext | ◐ | 仅 layer/asset/result；无 Experiment/Dataset/Model/WorkflowRun 身份 → WP-A |
| CommandRegistry | ✔ (wb9 M2 单一权威) | 新命令族注册即可 |
| ActiveViewHost/QgisDisplayManager | ✔ | N 视图联动组合服务缺失 → WP-E |
| DataManagerPanel/WorkspaceBrowser | ✔ (8.0/9.0) | — |
| InspectorHost | ✔ (+溯源 section) | 对象链接解析收敛 → WP-A |
| TaskPanelHost/history | ✔ | 算子目录搜索/收藏/过滤 → WP-F |
| schema forms | ✔ (5.0) | — |
| cartography/layout | ◐ | MapSpec 零 GUI 面（grep 证）→ WP-B |
| plots/widgets | ◐ | layer 绑定一次性 widget；无 typed source → WP-C |
| GuidedWorkflow/Lab | ✔ (#949/#952) | role/mode 投影核查 → Phase 5 核查项 |
| help/F1 | ✔ | 新面板接 help catalog |
| plugin declarative UI | ✔ (wb9 M8) | — |
| agent context（UI→agent） | ✘ | mcp_server.h 无 selection/context → WP-A |
| agent→UI（plan→canvas） | ✔ | test_agent_canvas_sync |

## B. 统一 selection 模型（WP-A 交付定义）

| 能力 | 状态 |
|---|---|
| stable typed ObjectRef | ✘ → 新 |
| primary + related selection | ◐（列表有，primary 无） |
| experiment/dataset/model/workflow 事实 | ✘ |
| provenance links 统一解析 | ◐（ProvenanceSection 私有）→ 收敛 |
| agent context projection | ✘ |

## C. 多视图（WP-E）

| 能力 | 状态 |
|---|---|
| 多视图创建/管理 | ✔ (display manager) |
| dual viewport sync | ✔ |
| N 视图 linked extent/cursor | ✘ |
| synchronized layer visibility | ✘ |
| before/after swipe（pixmap） | ✔ (comparison_widget)；地图级 swipe ✘（评估后定） |
| temporal frame | ✔ (temporal workbench) |
| spectral cursor | ◐（profile widget 单点；联动缺失） |
| map/layout coexistence | ✔ (workbench benches + layout designer) |

## D. Visual Analytics（WP-C）

| 组件 | 现状 | 平台化 |
|---|---|---|
| histogram | ✔ widget（layer 绑定、自扫） | typed source + 有界采样新实现，旧 widget 保留 |
| scatter | ✘ | 新 |
| spectral curve | ✔ widget | VaSeries 收敛 |
| temporal curve | ◐（temporal 面板内联） | 新 host |
| boxplot | ✘ | 新 |
| zonal statistics 图 | ✘（算子有，图无） | 新（消费算子输出） |
| confusion matrix | ◐（post_classification_dialog 内联表） | 平台 host + 迁移消费者 |
| transition matrix | ◐（同上） | 同上 |
| class area | ✘ | 新 |
| linked brushing | ✘ | VaSelectionHub |
| 空态/错误/加载/cancel/token | ✘ | 契约内建 |

## E. Workflow 可视化编辑（WP-D）

| 能力 | 状态 |
|---|---|
| DAG viewer/editor | ✔ (pipeline_scene) |
| node status | ✔ (NodeStatus) |
| IO 类型徽标/兼容 | ✘ |
| 参数 inspector | ✘（双击行为待核） |
| preflight 错误/修复投影 | ✘（preflight 逻辑在 agent 工具侧） |
| run/cancel/resume | ✔（controller/coordinator） |
| history/provenance | ✔（history panel 7.0） |

## F. Processing UX（WP-F）

| 能力 | 状态 |
|---|---|
| 算子执行权威 | ✔ TaskCenter |
| catalog 搜索/收藏/最近 | ✘ |
| capability 过滤 | ✘（#950 knowledge 只读可用） |
| 参数表单 | ✔ SchemaForm 5.0 |
| live availability | ✔ ContextRules |
| 资源估计 | ✔ estimate 既有 |
| 输出预览 | ◐（AssetPreviewService；结果面板 RsResultSummary） |

## G. Cartography 集成（WP-B）

| 能力 | 状态 |
|---|---|
| compose/preflight/repair/export 引擎 | ✔（headless/agent） |
| workflow node contract | ✘（C-1） |
| GUI compose/预览/修复/导出 | ✘ |
| 结果→MapSpec input | ✘ |
| 组件/模板目录（数据侧） | ✔（data/cartography） |
| 图表/图例/多页（声明侧） | ✔（引擎） |
| export evidence | ✔（引擎） |

## H. Agent 共存（WP-A）

| 能力 | 状态 |
|---|---|
| agent 读 selection/context | ✘ |
| agent→WorkflowIR→UI | ✔ |
| 手工编辑后 agent 继续 | ◐（session 参数快照在；无 context 感知） |
| 动作过 command/tool authority | ✔ |

## I. Lab 投影 / J. a11y / K. 视觉证据：Phase 5 核查矩阵（见 PLAN.md）
