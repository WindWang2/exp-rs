# ARCHITECTURE — 9.0 增量的设计决策

既有权威边界（全部维持，不新增平行实现）：

```
Pi (唯一 agent runtime)
WorkflowRunCoordinator → TaskCenter → JobEngine → Executor/Operator   (唯一调度链)
QGIS (唯一地图/布局渲染权威)   src/geospatial (唯一 I/O 权威)
Dataset/Experiment stores (元数据权威)   既有 registries (能力注册权威)
```

## D1 — WorkbenchStateModel（M1）：单一事实源，规则投影

现状：`SelectionContext` 聚合 selection/raster/vector/edit/governance facts，
`ContextRules` 纯函数推导可用性。缺口：工程生命周期（no project/empty/loaded）、
active view、tool mode、panel visibility、任务细分状态散落在各 host 手写判断。

决策：
- 新增 `src/app/workbench/workbench_state.h/.cpp`：`WorkbenchStateModel`（QObject），
  持有可枚举的顶层状态（project phase、task phase、active view id、tool mode、
  editable/editing、broken-layer 计数），由既有权威事件喂入（QgsProject read/cleared、
  TaskCenter 信号、display manager view 事件、map tool 切换信号）。
- `SelectionContextSnapshot` **不搬家不重命名**；`WorkbenchStateModel` 输出
  `WorkbenchFacts`，与 `ContextFacts` 在规则层组合——避免大爆炸重构。
- 所有 UI enable/disable/empty-state 切换**只允许**从 rules 投影；手写
  `setEnabled(...)` 条件在 M1 范围内收敛到投影函数（按调用点逐个迁移，测试锁定）。

不变量：状态模型是**投影聚合器**，不是第二权威——每个 fact 有唯一写入源
（project 事件只来自 project 层，task 只来自 TaskCenter…），模型只做合并与广播。

## D2 — Command & Shortcut Authority（M2）：消灭双权威

- `main_window_menus.cpp` 的 project/edit/view 菜单 action 改为经
  `CommandRegistry::action(id, installShortcut)` 取 projection；裸 addAction 仅保留
  registry 尚无 command 的项（并在 M2 把它们登记为真 command）。
- workflow 编辑器命令（`workflow.new/open/save/run/stop`）进入 registry；
  pipeline dock 工具栏消费 projection，tooltip 不再手写快捷键文本——由
  `QAction::shortcut()` 事实生成（`toolTip` 组装 helper）。
- 新增机械 gate 测试 `test_command_surface_consistency`：
  1) registry 内 shortcut 无重复（注册期已有，测试锁行为）；
  2) shell 构建后遍历 `QMenu`/`QToolBar` 所有 QAction：凡持 canonical shortcut 的，
     必须是 registry projection（或明确白名单：attribute table 的
     WidgetWithChildrenShortcut 局部快捷键）；
  3) empty-state/help 文案中出现的每个 `Ctrl+X` 字符串，能在 registry 中找到
     对应绑定（防 #882 复发）。

## D3 — Async callback 安全投递（M0 沉淀）

各处手写 `QPointer self + epoch + invokeMethod(qApp,…)` 模式抽成
`src/app/workbench/ui_callback.h`：`marshalToWidget(widget, fn)`（QPointer 守卫 +
目标线程投递；widget 死亡即丢弃）。**只在新代码与修过的路径强制使用**，不做全库
机械替换（避免大面积扰动）；M0 审计发现的缺陷路径逐个迁移并记录 REVIEW_LOG。

## D4 — SchemaEnumProvider 生产装配（M6）

- provider 实现 `src/app/shell/workbench_enum_provider.*`：只读聚合既有权威
  （DatasetStore 列举、ExperimentStore 列举、model registry、活动工程图层树），
  解析 `x-ui-enum-source` 语法（如 `datasets:`、`experiments:`、`models:`、
  `layers:raster`、`operators:`）。
- 装配点：`TaskPanelHost` 构建 SchemaForm 时注入（8.0 留下的空位）。
- 边界：provider 查询有上限（防 200k catalog 全量进 combo：返回前 N 条 +
  "…more" 提示 + 过滤参数），枚举解析失败降级自由文本（8.0 契约不变）。

## D5 — Large catalog UX（M7）

在 8.0 `AssetCatalogIndex`（20k 有界）上加：分页/窗口化读取投影
（`CatalogQuery{offset,limit,filter,sort}` 意图接口；本方向只定义 UI 侧接口与
客户端实现，store 侧 pushdown 经 data seam 后续接入）、200k 逻辑行 scale 测试
（合成索引，度量内存与响应上界）、selection preservation 跨翻页。

## D6 — Plugin declarative UI placement（M8）

消费已合入的 plugin host protocol 1.1 declarative UI 描述；placement 契约：
plugin 只声明（menu/toolbar/dock/prefs + command id + icon + label），宿主
`plugin_shell_ui` 创建宿主侧 wrapper widget，**原始 QWidget 永不跨界**；unload/
reload/crash 时宿主统一摘除并 disable 对应 command projection。

## 决策记录

- D-9.0-1: 不做"大重写菜单系统"——只把既有 registry 消费补全到主菜单与 workflow
  dock（增量、可测、可回退）。
- D-9.0-2: WorkbenchStateModel 是新类但**不是**新权威；与 SelectionContext 并列
  聚合器，规则层组合（避免既有 40+ 测试大迁移）。
- D-9.0-3: ui_callback helper 只向前兼容，不回溯替换全部手写点（审计驱动的定点迁移）。
