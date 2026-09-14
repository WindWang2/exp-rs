# OWNERSHIP — professional-workbench-visual-cartography-10

## 本 track 独占写入

- `src/app/workbench/**` — selection identity、context 扩展、agent context seam
- `src/app/visualanalytics/**` — 新建 VA 组件平台
- `src/app/cartography/**` — 新建制图 workbench 桥接层（dock/命令/预览）
- `src/app/workflow/**` — pipeline 节点 IO 徽标、参数 inspector、preflight 投影
- `src/app/shell/**` — 仅窄改：workflow_session_controller 的 cartography 分派、
  TaskPanelHost catalog UX、视图 link 控制器装配
- `src/app/main_window*.cpp` — 装配点窄改（dock/命令注册）
- `tests/**` — 本 track 新套件与回归
- `docs/**` — ui-architecture Part VI、本 track 文档
- `src/operators/` — **append-only**：`rs_operators_init.cpp` 增加一行注册 +
  新 operator family 文件（cartography bridge；单独 integration commit）

## 只读（权威归属）

| 目录 | 权威 | 归属 |
| --- | --- | --- |
| `src/core/`, `src/gui/` | QGIS vendored | upstream |
| `src/processing/` | TaskCenter/JobEngine/algorithm meta | execution tracks |
| `src/geospatial/` | I/O | data fabric tracks |
| `src/runtime/` | chunk/GPU/worker | execution tracks |
| `src/dataset/`, `src/experiment/` | stores | dataset/experiment tracks |
| `src/agent/cartography/`, `src/agent/mapspec/` | 制图引擎（复用目标） | cartography platform |
| `src/agent/` 其余 | MCP/tool catalog | agent tracks（窄集成 seam 除外） |
| `src/workflow/` | WorkflowIR 类型/序列化 | execution plane（窄缝：仅必要时 append 类型投影） |
| `resources/` | QSS theme | 与 design_tokens 同步规则约束 |

## 共享文件冲突表

| 文件 | 其他写入方 | 缓解 |
| --- | --- | --- |
| `src/operators/rs_operators_init.cpp` | 各算子 track | append-only 单行注册，独立 integration commit |
| `tests/CMakeLists.txt` | 全部 track | append-only 追加 target 块 |
| `src/app/CMakeLists.txt` | UI 域 track（本系列唯一） | 正常编辑；rebase 时优先保全双方新增行 |
| `docs/ui-architecture.md` | workbench 系列自身 | Part VI 追加 |
| `resources/styles*.qss` | theme 域 | 仅在 token 缺失时按规则 4 追加，两文件同步 |
| `.gitignore` | 新 track | 追加本 track 三行 |

## 不越界承诺

- 不重写核心算法、scheduler、TaskCenter/JobEngine 内部。
- 不给 agent 新增执行侧工具（context 投影是只读 seam）。
- 不在 UI 复制 MapSpec compiler / compose solver / export 逻辑。
