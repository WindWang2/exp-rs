# 统一算法引擎 + 后台任务列表

**日期:** 2026-07-21  
**状态:** 设计已批准（brainstorming）  
**产品:** SICNU GEO RS / RS Studio  
**相关:** `docs/superpowers/specs/2026-07-21-ui-workflow-engine-design.md`（Workflow / Ribbon / TaskPanel）

---

## 1. 问题与目标

### 1.1 问题

- 算法实现分散：`RSOperator`、对话框内 GDAL/lambda、`QgsTask`、Processing 工具箱多套入口。  
- 后台执行路径不统一：`AsyncGdalRunner`、自管 `std::thread`、`QgsTask` 等，无法在一处观察/取消。  
- 日志与任务生命周期脱节：`LogPanel` 是全局消息流，缺少「某次算法运行」的绑定日志。  

### 1.2 目标

1. **算法模块化**：每个算法是引擎中的独立模块（标准为 `RSOperator` + schema + 注册）。  
2. **调用统一后台化**：所有算法调用经 `JobEngine.submit`，进入统一任务列表。  
3. **任务与日志清晰**：任务有明确状态机；每任务有绑定日志；系统日志与任务日志分离。  
4. **全量覆盖**：RSOperator 族、App 对话框路径、分类/几何校正/OBIA 模块、QGIS Processing 工具箱。  

### 1.3 非目标

- 分布式 / 多机调度  
- Job 落盘与进程重启后队列恢复（本轮仅进程内）  
- 可视化 Model Builder  
- 显示类操作（如显示拉伸改 renderer）不作为算法 Job  

---

## 2. 产品决策（已确认）

| 决策点 | 选择 |
|--------|------|
| 迁移深度 | **全量**（分波次交付，架构一次定死） |
| 算法宇宙 | **全部**：RSOperator + App 对话框 + 模块内核 + Processing 工具箱 |
| 并发 | **有限并行**，默认 **3** worker（可配 2–4）；支持 `exclusive` |
| 任务 UI | **统一任务 Dock**：列表 + 选中任务日志 |
| 实现路径 | **路径 1**：在 `RSOperator` 上叠 JobEngine（不推倒重写、不以 QgsTaskManager 为唯一调度） |

---

## 3. 架构

### 3.1 分层

```
┌─────────────────────────────────────────────────────────────┐
│ Presentation                                                 │
│ Ribbon · TaskPanel · Dialogs · Module windows · Toolbox · MCP│
├─────────────────────────────────────────────────────────────┤
│ JobEngine（新，src/jobs/ 或 src/engine/）                      │
│ submit / cancel / list / events · Queue · Workers · Logs     │
├─────────────────────────────────────────────────────────────┤
│ Algorithm modules                                            │
│ RSOperatorRegistry · ProcessingAdapter · (legacy wrappers)   │
├─────────────────────────────────────────────────────────────┤
│ Kernels: analysis · GDAL · OTB CLI · OpenCV · QGIS algos     │
└─────────────────────────────────────────────────────────────┘
```

- **JobEngine 不得依赖 Qt Widgets**（可用 Qt Core 信号若项目惯例允许；优先纯 C++ + 回调，UI 用 QObject 桥接）。  
- Presentation **禁止**直接 `std::thread` 跑算法或自建长生命周期 runner。  

### 3.2 与现有组件

| 组件 | 关系 |
|------|------|
| `RSOperator` + Registry + schema | **算法实现标准**；补齐未注册算法 |
| `WorkflowRuntime::runStep` | 改为 `JobEngine.submit`；session 记录 `jobId` |
| `AsyncGdalRunner` / dialog 自管线程 | **退役**（W6） |
| 分类 / georef / OBIA 的 QgsTask | 改为 submit 或 Job 包装同一生命周期 |
| `LogPanel` | 系统/Qt/未绑定消息；任务细节以 Job 日志为准 |
| Processing Toolbox | 确认参数后 `submit` 适配 Job |

### 3.3 依赖方向

```
app/gui → jobs (JobEngine facade) → operators → kernels
MCP → jobs
workflow → jobs
```

禁止 operators → app。  

---

## 4. 数据模型与 API

### 4.1 JobRequest

| 字段 | 说明 |
|------|------|
| `algorithmId` | 如 `rs:spectral_index`、`processing:gdal:warpreproject` |
| `params` | `Json::Value` 对象 |
| `title` | UI 显示名（可默认 displayName） |
| `source` | `ui` \| `task_panel` \| `dialog` \| `toolbox` \| `module` \| `mcp` \| `workflow` |
| `exclusive` | bool，重任务独占（等无其它 running 后独占 worker） |
| `clientTag` | 可选，调用方关联 id |

### 4.2 JobState

`queued` → `running` → `succeeded` | `failed` | `cancelled`

### 4.3 JobRecord

| 字段 | 说明 |
|------|------|
| `id` | 稳定字符串或单调 id |
| `request` | 原始请求摘要 |
| `state` | 见上 |
| `progress` | 0.0–1.0，未知时可用 -1 表示 indeterminate |
| `statusMessage` | 短状态文案 |
| `logLines` | `{ timestamp, level, text }[]` |
| `result` | 成功时 JSON（常含 `output` 路径） |
| `error` | 失败时消息 |
| `createdAt` / `startedAt` / `finishedAt` | 时间戳 |

### 4.4 JobEngine API

```
submit(JobRequest) → jobId
cancel(jobId) → bool
snapshot(jobId) → optional<JobRecord>
list(JobListFilter?) → vector<JobRecord>  // filter by state
setMaxWorkers(n)  // 2..4 clamp
// Observability (C++ callbacks or Qt bridge signals):
//   jobUpdated(jobId)
//   jobFinished(jobId)
```

**执行路径：**

1. 入队 `queued`  
2. Worker 取 job → `running`  
3. 解析 `algorithmId`：  
   - `RSOperatorRegistry::create` → `run(params, context)`  
   - 或 ProcessingAdapter  
4. context 进度/日志 → 写入 JobRecord 并 `jobUpdated`  
5. 成功/异常/取消 → 终态 + `jobFinished`  

**取消：** 设置 context 取消标志；已结束的 job 忽略 cancel。  

**Exclusive：** 调度器在存在 exclusive queued/running 时按「排空再独占」策略运行（实现需文档化于代码注释）。  

---

## 5. 算法模块规范

### 5.1 标准模块（RSOperator）

每个模块必须：

1. 唯一 `name()` / 注册 id  
2. `schema()` + `displayName()` / `group()`  
3. `run()` 无线程亲和 GUI、可取消  
4. 静态 `REGISTER_RS_OPERATOR` 或 init 注册  

### 5.2 Processing 适配

- id 约定：`processing:<providerId>:<algorithmId>` 或与现有 toolbox id 对齐并文档化。  
- Adapter 在后台线程创建 `QgsProcessingContext`（遵循 QGIS 线程规则；若必须主线程片段，仅 marshall 必要部分，进度仍回报 Job）。  
- 参数从 JobRequest JSON 映射到 `QVariantMap`（映射表可分算法增量完善）。  

### 5.3 模块内核（分类 / 几何校正 / OBIA）

- 将「一次用户可感知的重计算」注册为 job（如 `module:classify:apply`、`module:georef:warp`）。  
- 实现可先 **包装现有 Task 类** 为 operator/run，再逐步内聚。  
- 必须出现在统一任务列表，可取消（在底层支持的前提下）。  

---

## 6. 任务 Dock 与日志 UI

### 6.1 `RsJobPanel`（Dock）

- **位置：** 底部（可与日志区分；默认显示或产品壳可默认开）。  
- **左：** 任务列表 — 标题、algorithmId、状态、进度、来源。  
- **右：** 选中任务日志 + 操作：取消；成功时「加载输出到图层」（若 result.output 为栅格路径）。  
- **工具条：** 筛选（全部 / 运行中 / 失败）、清空已完成、打开时跟随最新 running。  
- **状态栏：** 可选「任务 n 运行中」点击 raise Dock。  

### 6.2 日志通道

| 通道 | 用途 |
|------|------|
| Job 日志 | 该次运行的 INFO/WARN/ERROR（主路径） |
| LogPanel | 应用启动、Qt、插件、未绑定 job 的消息 |
| 镜像（可选配置） | Job 日志同时 `QgsMessageLog` tag=`job:<id>` |

### 6.3 文案与错误

- 算法失败：job `failed` + error 字符串；UI Toast/状态栏 + Dock 标红。  
- **禁止**算法线程弹 `QMessageBox`。  

---

## 7. 调用入口改造原则

```
用户/Agent 意图
  → 校验 UI 层（可选）
  → JobRequest
  → JobEngine.submit
  → 订阅 jobUpdated / jobFinished
  → 成功处理（加图层、刷新 session artifact）
```

| 入口 | 行为 |
|------|------|
| TaskPanel Run | submit；进度绑 panel |
| 处理对话框 Run | submit；对话框可关，任务仍在 Dock |
| Toolbox 双击 | 参数确认后 submit |
| 模块主操作 | submit |
| Workflow runStep | submit；session 存 jobId |
| MCP | `jobs.submit` / `jobs.status` / `jobs.log` / `jobs.cancel` |

显示拉伸等 **非算法** 路径保持直接改 renderer。  

---

## 8. 迁移波次

| 波次 | 交付 | 验收 |
|------|------|------|
| **W1** | `JobEngine` + 单测 + `RsJobPanel` 骨架 + mock operator | 列表可见 queued→running→succeeded |
| **W2** | 全部现有 RSOperator 经引擎；TaskPanel / WorkflowSessionController 改 submit | 光谱指数等全路径 + 任务日志 |
| **W3** | 剩余 dialog / ImageEnhancement 等处理路径迁 submit 或补 operator | 无 dialog 自管算法长线程 |
| **W4** | 分类 / 几何校正 / OBIA 主计算 → job | Dock 可见模块任务 |
| **W5** | Processing 适配器 + toolbox 接线 | 工具箱算法进队列 |
| **W6** | 删除 Async* 旧路径；文档与日志 tag 统一 | 无双轨后台执行 |

波次可多 PR，但 **不得** 在 W2 之后再新增绕过 JobEngine 的算法调用。  

---

## 9. 目录建议

```
src/jobs/
  job_types.h
  job_engine.h/.cpp
  job_worker.h/.cpp          // optional internal
  processing_job_adapter.h/.cpp
tests/test_job_engine.cpp

src/app/shell/
  rs_job_panel.h/.cpp       // Dock UI
  job_engine_qt_bridge.h/.cpp  // signals to UI if engine is non-Qt
```

CMake：`sicnu_jobs` 静态库，link `sicnu_operators`；app link `sicnu_jobs`。  

---

## 10. 测试策略

- **单元：** 并行上限、exclusive、取消、失败、日志追加顺序  
- **适配：** mock Processing algorithm  
- **集成：** TaskPanel submit → job finished → output path  
- **手工：** 多任务排队、取消、失败重跑（若实现）、Dock 筛选  

---

## 11. 风险与缓解

| 风险 | 缓解 |
|------|------|
| Processing 线程模型复杂 | Adapter 分算法接入；先覆盖常用 GDAL/native |
| 全量 B 范围膨胀 | 严格波次；W1–W2 先闭合主路径 |
| 双轨残留 | W6 删除旧 runner；code review 禁止新旁路 |
| UI 卡顿 | Engine 回调 marshall 到主线程批量更新 |

---

## 12. 成功标准

1. 产品内算法执行（含工具箱）均出现在统一任务列表。  
2. 可对 running 任务取消（底层支持时）。  
3. 选中任务可查看完整绑定日志。  
4. TaskPanel / 主流程 dialog 不再自建算法线程。  
5. `test_job_engine` 覆盖调度核心；主路径手工验收通过。  

---

## 13. Key Decisions

1. **JobEngine 叠在 RSOperator 上** — 复用模块化资产。  
2. **有限并行 + exclusive** — 教学机稳定与吞吐平衡。  
3. **统一任务 Dock + Job 绑定日志** — 任务管理清晰。  
4. **Processing 用适配器纳入全量** — 不要求立刻重写所有算法体。  
5. **显示增强不是 Job** — 与图层属性一致。  

---

## 14. Open Questions

无阻塞项。实现时可定：

1. Job id 格式（UUID vs 单调整数）。  
2. 默认是否镜像 Job 日志到 LogPanel。  
3. MCP 工具命名最终表。  

---

## 15. PR Plan

| PR | 标题 | 依赖 |
|----|------|------|
| PR1 | JobEngine 核心 + 测试 | — |
| PR2 | RsJobPanel + Qt bridge | PR1 |
| PR3 | TaskPanel / workflow 改 submit | PR1–2 |
| PR4 | Dialog 路径迁移 + 补 operator | PR3 |
| PR5 | 模块 classify/georef/obia job 化 | PR1–2 |
| PR6 | Processing adapter + toolbox | PR1–2 |
| PR7 | 删除旧 runner + 文档 | PR3–6 |

---

## 16. 参考

- `src/operators/framework/rs_operator.h`  
- `src/workflow/workflow_runtime.h`  
- `src/app/log_panel.h`  
- `docs/superpowers/specs/2026-07-21-ui-workflow-engine-design.md`  
