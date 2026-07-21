# UI 产品重设计 + 统一工作流引擎

**日期:** 2026-07-21  
**状态:** 设计已批准（brainstorming）  
**产品:** SICNU GEO RS / RS Studio  
**深度:** 产品级重设计（路径 3：工作流引擎驱动 UI）  

---

## 1. 问题与目标

### 1.1 问题

当前主界面仍偏「经典 GIS 菜单 + 大量独立对话框」：

- 实验流程（数据 → 预处理 → 分析 → 分类 → 制图）在导航上不显式，学生找工具成本高。
- 栅格处理对话框各自为政，虽有 `RasterProcessingDialogBase`，但缺少统一的输入/参数/输出/运行节奏与 schema 驱动表单。
- 分类、几何校正等模块已有局部 Stepper/Session 思路，但与主壳、对话框、Agent/MCP 未共用同一状态模型。
- 主题（Slate Light）与图标资源已存在，组件语言未在主壳与模块工作区强制统一。

### 1.2 目标

1. **主壳按遥感实验流程重组**：六段 Ribbon + 画布中心 + 右侧任务面板。
2. **简单工具人性化**：右侧面板固定「输入 → 参数 → 输出 → 运行」；默认图层、默认输出名、行内校验、进度与取消。
3. **复杂模块流程化**：分类 / 几何校正 / OBIA 使用专用工作区窗口，统一 Stepper、软门禁、主题 token。
4. **统一工作流引擎**：声明式 workflow + session 驱动 GUI（及后续 CLI/MCP），算子层继续基于现有 `RSOperator`。
5. **可分期交付**：先 Runtime 与主壳，再批量工具迁移，再模块挂接。

### 1.3 非目标（本设计明确不做）

- 可视化 Model Builder（画线编排）编辑器。
- 通用 DAG 并行调度 / 分布式执行。
- 替换全部 QGIS 原生对话框（属性表、通用投影选择器等）；范围限于 **RS 产品面**。
- 重写分类 / 几何校正算法内核；仅把会话与 UI 迁到 Runtime。
- 在一个 PR 内完成全库迁移。

---

## 2. 产品决策（已确认）

| 决策点 | 选择 |
|--------|------|
| 范围 | 全做：主壳 + 对话框族 + 分类 + 几何校正 + 横切体验 |
| 深度 | 产品级重设计 |
| 主壳范式 | 流程功能区 **Ribbon** |
| 简单工具形态 | **右侧任务面板**（画布始终可见） |
| 复杂模块形态 | **专用工作区窗口** + 统一 WorkflowChrome |
| Ribbon 分段 | **六段**：工程 · 数据 · 预处理 · 分析 · 分类/解译 · 制图 |
| 实现路径 | **路径 3**：统一 Workflow Runtime 驱动 UI（非仅换皮） |
| 门禁策略 | **软引导**：可进任意步；主操作在缺前置时禁用并提示 |
| 主题 | 延续 **Slate Light**，抽成 design tokens |

---

## 3. 总体架构

### 3.1 分层

```
┌─────────────────────────────────────────────────────────────┐
│  UI Shell                                                    │
│  Ribbon · TaskPanelHost · Workspace windows · Theme tokens   │
├─────────────────────────────────────────────────────────────┤
│  Workflow Runtime（新增 src/workflow/）                        │
│  Definition · Step · Session · Gate · Artifact · Runner      │
├─────────────────────────────────────────────────────────────┤
│  Operator 层（已有，扩展 UI/workflow 元数据）                    │
│  RSOperator · schema() · RSOperatorRegistry · async runners  │
├─────────────────────────────────────────────────────────────┤
│  Core / Analysis / GDAL / OTB / OpenCV kernels                 │
└─────────────────────────────────────────────────────────────┘
```

- **Runtime 不得依赖 Qt Widgets**（可依赖 Qt Core 仅当项目已有先例且必要；优先纯 C++ + JSON）。
- UI 订阅 session 状态变化并投影为控件；业务门禁与完成条件在 Runtime 计算。

### 3.2 三类可执行单元

| 类型 | 定义 | 宿主 |
|------|------|------|
| Atomic tool | 单步 `RSOperator`（或单步 workflow） | 主壳右侧 `TaskPanelHost` |
| Lab workflow | 多步有序图（可软跳步） | 主壳内进度条 + 侧栏当前步，或工作区 |
| Module workspace | 长会话交互（分类、几何校正、OBIA） | 专用工作区窗口；内部仍由 session 驱动 |

### 3.3 与现有资产的关系

| 现状 | 关系 |
|------|------|
| `sicnu::operators::RSOperator` + registry + schema | **算子底座保留**；补充 `x-ui-*` / workflow 元数据 |
| `RasterProcessingDialogBase` | 过渡期保留异步执行；目标态 UI 壳退役，逻辑进 TaskPanel + Runner |
| `RsClassifyWorkflowController` / Stepper | 映射为 definition + session；UI 只读 session |
| `RsClassifySessionState` / georef session | 包装或迁入 `WorkflowSession` 扩展数据 |
| Slate Light / `docs/design/ui` 图标 | 主题与图标资产复用 |
| Processing toolbox / 算法对话框 | 逐步改为 open workflow/operator，避免第三套入口 |

---

## 4. 主壳信息架构

### 4.1 布局

| 区域 | 职责 |
|------|------|
| 顶栏 Ribbon | 六段 Tab；组内工具；复杂模块入口 |
| 左 | 图层树 |
| 中 | 地图画布（始终可见） |
| 右 | `TaskPanelHost` |
| 底 | 状态栏（CRS、坐标、任务、提示）；日志 dock 可选 |

### 4.2 Ribbon 六段归属

| Tab | 内容 |
|-----|------|
| **工程** | 新建/打开/保存、偏好、实验报告、布局入口 |
| **数据** | 导入栅格/矢量、STAC、CRS、波段提取；**几何校正工作区入口** |
| **预处理** | 辐射/大气、增强、滤波、融合、镶嵌等 |
| **分析** | 光谱指数、波段运算、PCA、变化检测、地形 |
| **分类/解译** | 分类工作区、OBIA 入口、精度相关快捷入口 |
| **制图** | 布局、导出地图、标注相关 |

规则：

1. Ribbon 只触发 `open(definitionId | operatorId)`，不嵌入业务实现。
2. 同一 operator 只出现在一个主 Tab；相关推荐用侧栏链接，不重复堆图标。
3. 复杂模块按钮打开 workspace，并 `WorkflowRuntime.open(...)`。

### 4.3 TaskPanelHost

**打开来源**

- Ribbon 工具点击
- 图层上下文「用此图层运行…」（预填输入）
- 多步 workflow 的当前步需要参数时自动切换

**Atomic 工具四段结构（固定顺序）**

1. **输入** — 图层/文件；默认 = 当前选中且类型兼容的图层  
2. **参数** — schema 字段；`x-ui-advanced` 默认折叠  
3. **输出** — 路径 + 浏览；默认名 `{tool}_{inputstem}.tif`（或 schema 指定扩展名）  
4. **运行** — 主按钮、帮助、进度；运行中禁止重复提交与随意关闭  

**面板状态机**

`idle` → `invalid`（行内提示）→ `running` → `success` | `failed`

- `success`：提供「加载到图层树」（默认勾选策略可配置）  
- `failed`：错误摘要 + 重试  

### 4.4 Schema → 表单

在 `RSOperator::schema()` / `metadata()` 上约定扩展（无注解时启发式回退）：

| 注解 | 含义 |
|------|------|
| `x-ui-order` | 排序 |
| `x-ui-group` | `input` \| `params` \| `output` |
| `x-ui-widget` | `layer-raster` \| `layer-vector` \| `file` \| `enum` \| `band` \| `crs` \| `number` \| `bool` \| `string` |
| `x-ui-advanced` | 高级区折叠 |
| `x-ui-depends-on` | 条件显示 |

表单生成器：`src/app/workflow_ui/`（或 `src/gui/workflow/`）读取 schema，产出 Qt 控件；**不**在 operator 内创建 QWidget。

### 4.5 主题 tokens

延续 Slate Light：

| Token | 值 |
|-------|-----|
| bg.app | `#F8F9FB` |
| accent | `#7E57C2` |
| surface | `#FFFFFF` |
| border | `#E0E4E8` |
| text | `#2D3436` |
| text.muted | `#636E72` |
| selection.bg | `#F0E7FF` |

主壳、TaskPanel、各 workspace 共用同一 QSS/token 片段；主按钮 / 次按钮 / 危险 / 步进条 / 禁用态有统一样式类名。

---

## 5. Workflow Runtime

### 5.1 数据模型

**WorkflowDefinition**（不可变蓝图）

- `id`（如 `tool.rs.spectral_index`、`lab.classify.supervised`）
- `title`、`description`、可选 `labTag`
- `host`: `task_panel` | `workspace`
- `workspaceKind`（当 host=workspace）：`classify` | `georef` | `obia` | …
- `steps[]`

**WorkflowStep**

- `id`、`title`
- `kind`: `operator` | `interactive` | `review` | `composite`
- `operatorId`（kind=operator 时）
- `gates[]`：前置条件 + `hint` 文案
- `completion`：如何标记完成（artifact 存在 / 用户确认 / 算子成功）
- 可选默认参数模板

**WorkflowSession**

- `sessionId`、`definitionId`
- `currentStepId`
- `completedStepIds`
- `mode`: `wizard` | `expert`
- `paramsByStep`（JSON）
- `artifacts`（name → path/metadata）
- `dirty`
- `extension`（模块私有状态指针/JSON，如 ROI 模型、GCP 列表引用）

**Artifact**

- 命名产物，供后续 gate 与导出清单使用。

**Gate**

- 纯函数：`(session) → ok | hint`  
- 内置谓词示例：`hasArtifact`、`paramNonEmpty`、`minCount`、模块注册的自定义谓词。

### 5.2 Runtime API

面向 GUI 与后续 MCP/CLI 的同一契约：

```
open(definitionId, OpenContext?) → sessionId
session.state() → SessionSnapshot
session.goto(stepId)
session.setParams(stepId, Json)
session.canRun(stepId) → { ok, hints[] }
session.run(stepId) → jobId   // async
session.cancel(jobId)
session.setMode(wizard|expert)
session.markStepComplete(stepId)  // review/interactive
session.close() → needsConfirmIfDirty
listSessions() / get(sessionId)
```

`OpenContext` 可带：预选图层、工程路径、从主壳传入的 CRS 等。

### 5.3 执行

- `operator` 步：校验 params → `RSOperatorRegistry` 查找 → 后台线程 `run` → 写 artifact → 更新 completed  
- 进度/取消：复用 `RSOperatorContext`  
- UI：`TaskPanel` / workspace 通过信号或回调观察 job  

### 5.4 软门禁（全局规则）

1. `goto` 任意步始终允许（专家与向导相同，除非未来产品改硬门禁）。  
2. `run` / Apply / 破坏性导出：`canRun` 为 false 时禁用主按钮，显示 `hints`。  
3. 可选「前往前置步骤」导航链接。  
4. 专家模式：不隐藏高级控件；门禁仍作用于主操作。

### 5.5 定义注册

- 代码静态注册（C++ 工厂）为主；可选加载 `data/workflows/*.json` 作为补充。  
- Atomic tool：可为每个 operator 自动生成单步 definition，或显式列出「上架到 Ribbon」的清单（**推荐显式清单**，避免工具箱噪音）。

### 5.6 目录建议

```
src/workflow/
  workflow_definition.h/cpp
  workflow_session.h/cpp
  workflow_runtime.h/cpp
  workflow_gate.h/cpp
  workflow_registry.h/cpp
  workflow_runner.h/cpp      // 调 operator / 管 job
tests/workflow/              // 纯逻辑 ctest
```

UI：

```
src/app/shell/               // 或挂在 main_window_* 
  ribbon_controller.*
  task_panel_host.*
  schema_form_builder.*
  workflow_chrome.*          // Stepper + mode toggle 共用
```

---

## 6. 模块工作区挂接

### 6.1 通用模式

```
Ribbon 入口
  → Runtime.open(definitionId)
  → 打开对应 Workspace 窗口（host=workspace）
  → WorkflowChrome 绑定 session（Stepper、模式、门禁提示）
  → 中部画布 + 右侧/底部 Step 面板
  → 导出：artifacts → 主工程图层树
  → 关闭：dirty 确认；session 结束（序列化可作为后续增强）
```

### 6.2 监督分类

- Definition：`lab.classify.supervised`（与现有 7 步对齐）  
  1. 分类体系（interactive）  
  2. 样本（interactive）  
  3. 样本评价（review）  
  4. 训练-分类（operator / 现有 task）  
  5. 精度（review）  
  6. 后处理（operator 族）  
  7. 输出（review）  
- 吸收并兼容：`docs/superpowers/specs/2026-07-19-classification-workflow-design.md` 中已确认的产品决策；本设计将其 **Runtime 化**，不另起一套步骤语义。  
- `RsClassifyWorkflowController`：逐步改为 Runtime 适配层或删除重复状态机。

### 6.3 几何校正

- Definition：`lab.georef.image_to_map`（示意步骤，实现时与现有双窗流程对齐）  
  打开影像 → GCP 采集 → 变换模型 → 残差检查 → 重采样/写出 → 加载结果  
- 现有 `QgsGeoreferencerMainWindow` / twin canvas：保留交互能力，状态写入 session artifacts（GCP、RMS、输出路径）。

### 6.4 OBIA / STAC / 其它

- OBIA：workspace definition，入口在「分类/解译」。  
- STAC：可为 `task_panel` 或轻量对话框式 panel（仍建议走 session 以便记录产物）。  
- 偏好设置：保持独立对话框，不强制 workflow。

---

## 7. 旧对话框迁移策略

| 阶段 | 策略 |
|------|------|
| 过渡 | 未迁移工具：仍可用 `RasterProcessingDialogBase`；或薄适配「dialog 内容嵌侧栏」 |
| 迁移 | 优先高频：光谱指数、增强、滤波、波段运算、融合、镶嵌、变化检测、地形 |
| 目标 | RS 产品工具 100% TaskPanel 或 Workspace；菜单/工具箱入口统一 `open(...)` |
| 基类 | 保留异步/校验工具函数到 `async_*` 或 runner；UI 组装离开 dialog 基类 |

禁止：新 RS 工具只加 QDialog 而不注册 operator + definition。

---

## 8. 人性化交互规范（横切）

1. **默认值**：输入图层、输出路径、常用枚举合理默认。  
2. **校验**：优先行内；阻塞错误再弹窗。  
3. **反馈**：运行中进度与取消；成功可加载图层；失败可重试。  
4. **帮助**：短描述横幅 + Help 详文（复用 `dialog_help_catalog` / operator description）。  
5. **关闭**：running 时拦截；dirty session 确认。  
6. **文案**：中文主 UI（与现有菜单一致）；工具名可中英对照。  
7. **可访问性**：主按钮可达、Tab 序合理、状态不仅靠颜色。

---

## 9. 分期交付（PR 切片）

| 期 | 交付 | 验收要点 |
|----|------|----------|
| **W0** | `src/workflow` 核心 + ctest（definition/session/gate/mock run） | 无 GUI 单测绿 |
| **W1** | Ribbon 壳 + TaskPanelHost + schema 表单 + 3～5 个 atomic 工具 | 主路径可演示 |
| **W2** | 预处理/分析批量迁移；旧入口改 open | 清单内工具无独立烂对话框 |
| **W3** | 分类 workspace 接 Runtime | 7 步与软门禁；结果回主壳 |
| **W4** | 几何校正 workspace 接 Runtime | 双窗主路径 + 导出回主壳 |
| **W5** | OBIA/STAC/主题横切；可选 MCP session API | 一致性与回归 |

依赖顺序：W0 → W1 → (W2 ∥ 准备 W3) → W3 → W4 → W5。

---

## 10. 测试策略

- **Runtime**：纯 C++ 单元测试（gate、完成条件、参数、mock operator 成功/失败/取消）。  
- **表单生成**：schema fixture → 控件数量/分组（可 headless 或轻量 Qt test）。  
- **模块**：保留/扩展现有 classification / georef 相关测试；状态机断言改为 session 快照。  
- **手工**：Lab 主路径清单（增强 → 指数 → 分类向导 → 导出）；几何校正开图打点写出。

---

## 11. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 路径 3 前期无可见 UI | W0 控制在小 PR；W1 尽快出 Ribbon+面板演示 |
| 双状态机（旧 controller + Runtime） | W3/W4 设「单一真相」里程碑，禁止长期双写 |
| Schema 注解不全 | 启发式映射 + 迁移清单逐个补注解 |
| 主窗口代码膨胀 | Ribbon/TaskPanel 独立文件；遵守 main_window_* 拆分 |
| 范围膨胀到 Model Builder | 非目标条款；评审时拒绝 |

---

## 12. 成功标准

1. 新用户可按 Ribbon 六段完成一次「打开数据 → 预处理 → 分析 → 打开分类 → 导出」而无需在深层菜单中搜索。  
2. 高频 atomic 工具均在右侧面板完成，画布不被模态框长期遮挡。  
3. 分类与几何校正使用同一套 Stepper/软门禁/主题语言。  
4. Workflow Runtime 单测覆盖 gate/session/run；至少 3 个工具与 1 个模块由 session 驱动。  
5. 无新增「只 dialog、不注册 definition/operator」的 RS 工具。

---

## 13. Key Decisions

1. **引擎驱动 UI（路径 3）** — 长期 GUI/Agent 一致，代价是先建 Runtime。  
2. **Ribbon 六段 + 右侧 TaskPanel** — 流程可见且画布常在。  
3. **复杂模块独立窗口** — 控制主壳复杂度，用 Runtime 统一会话语义。  
4. **软门禁** — 兼顾教学引导与专家自由。  
5. **显式上架清单** — 不是每个 operator 都进 Ribbon。  
6. **复用 RSOperator** — 不重造算子层。  

---

## 14. Open Questions

无阻塞项。以下实现阶段再定即可：

1. Runtime 是否允许仅用标准库 + JsonCPP（推荐），或引入 Qt Core 信号。  
2. Session 磁盘序列化格式（W3+）。  
3. MCP 暴露 `session.*` 的具体工具名与权限（W5）。

---

## 15. PR Plan（实现时参考）

| PR | 标题 | 依赖 |
|----|------|------|
| PR1 | Workflow Runtime 核心 + 测试 | — |
| PR2 | Design tokens + Ribbon 壳 + TaskPanelHost 空壳 | — |
| PR3 | Schema 表单生成器 + Runner 接线 + 首批 3～5 工具 | PR1, PR2 |
| PR4 | 预处理/分析工具批量迁移 | PR3 |
| PR5 | 分类 workspace Runtime 化 | PR1, PR3 |
| PR6 | 几何校正 workspace Runtime 化 | PR1, PR3 |
| PR7 | OBIA/STAC/横切与清理旧对话框入口 | PR4–PR6 |

---

## 16. 参考文档

- `docs/superpowers/specs/2026-05-23-slate-light-theme-design.md`
- `docs/superpowers/specs/2026-07-19-classification-workflow-design.md`
- `docs/superpowers/specs/2026-07-15-georeferencer-v16-usability-polish-design.md`
- `docs/dialog-base-class.md`
- `src/operators/framework/rs_operator.h`
