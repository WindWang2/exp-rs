# 实验报告（Lab Report, `sicnu.labreport.v1`）

D5 轨道的交付：把 **操作日志 → experiment run → 溯源 → 可打印报告** 接成一个闭环。
教师拿到的报告必须能回答"这个结果是怎么来的"。

## 闭环怎么走

1. **执行**：学生在桌面端运行实验（任意 tracked pipeline，
   `WorkflowRunCoordinator::startTrackedPipeline`）。`RSOperator::execute` 继续把每个
   算子执行记入 `RSOperationLogger`（内存操作轨迹）。
2. **自动登记**（ADR 0143 语义，opt-out）：`LabRunRecorder`
   （`src/experiment/bridge/lab_run_recorder.h`）作为 coordinator 生命周期信号的排队消费者，
   把每次实验执行经 `ExperimentRunBridge` 登记为 `ExperimentStore` 里的一等 experiment run。
   打开工程时启用（工程旁 `.sicnu/lab/experiments.db`），QSettings
   `lab/autoRecordExperimentRuns=false` 可退出；退出后**新的**执行不再登记，已登记的
   执行仍会如实关闭。终态/中断事件时，操作轨迹（经 `RunEnvironment::redactSecretKeys`
   脱敏、按 ADR 0143 限量 256 条）作为 run 证据存入 `workflow.extra.operationTrail`。
3. **导出**：`Project ▸ 导出实验报告...`（`main_window_project.cpp` 的
   `exportLabReport`）用 `LabReportBuilder` 从 store + 操作轨迹构建
   **一份** `sicnu.labreport.v1` JSON 文档，`lab_report_writers` 把同一份文档渲染成
   Markdown 与自包含可打印 HTML；三个同基名文件一起落盘（`<名>.json/.md/.html`）。

## Schema（`sicnu.labreport.v1`）

JSON-first；Markdown/HTML 是同一文档的渲染，无格式独有信息。

| 段 | 内容 | 来源（只投影，不发明） |
| --- | --- | --- |
| `header` | reportId、labId/labName/objective、student、session、generatedAtUtc、softwareRevision、gitSha | store + 导出对话框（学号/session 预填自 QSettings，绝不编造） |
| `runs[]` | run 身份：状态、executionRef、算法、参数（脱敏）、dataset/split/model pins、seed、时间窗、三个指纹（config/execution/result）、产物、workflow 证据 | `ExperimentStore` |
| `steps[]` | 操作轨迹逐条：operator、params/result（脱敏）、success、错误、起止时间、耗时、`attribution` | `RSOperationLogger` 快照 |
| `statistics[]` | run 的 MetricRecord（protocol + 指标文档）原样 | store |
| `thumbnails[]` | 输出缩略图：内联 PNG data URL（≤512 px 长边）、sha256、来源路径；失败渲染以 `renderError` 如实呈现 | 现有 `renderRasterPreview` 有界预览路径 |
| `grade` | `status: "recorded"|"unavailable"`；recorded 必须带 `gradingRef` + `inline` | 类型化成绩缝（D4 未合入 → `unavailable` + 原因；绝不伪造分数） |
| `lineage` | 主 run 的祖先切片（`LineageGraph`）；无 dataset store 时退化为 experiment 边，`existence: "not-checked"` 如实标注 | `lineage.h` / store 边表 |
| `environment` | run 的 `RunEnvironment.redacted()`（允许清单 + 拒绝清单双保险，issue #789） | store |
| `replay` | `ReplayReadiness::assess` 原文：`level`（exact/compatible/best_effort/impossible）、逐依赖 checks、**blockers 明列** | `replay_readiness.h` |
| `warnings[]` | 缩略图超限/非 PNG 等拒绝原因 | builder |

### 确定性契约

同样输入 + 注入同样的 `header.generatedAtUtc` ⇒ 三种渲染字节级一致。
实现：QJson 键序确定；所有数组显式排序（runs 按终态时间→runId、steps 按起始时间→
原始序号、产物/边按端点元组）；凡经过哈希容器的数据（lineage 节点、环境变量）落盘前
重排。`generatedAtUtc` 是唯一允许不同的时间头。

### 步骤归属（诚实声明）

`RSOperatorContext` 不携带 run/step 身份，操作轨迹无法在记录时刻绑定执行。报告的
`steps[].attribution.policy = "time-window+operator-name"`：轨迹记录的起始时间落在某 run
的执行窗口内 → 归属该 run（`quality: "exact"`）；窗外 → `unattributed`；多候选 →
`ambiguous` 且不指派。归属策略写在每个 step 与 ADR 0146 里，绝不静默指派。

### 成绩缝（D4）

`LabGradeEmbedding`：D4 合入后以 `status:"recorded"` + `gradingRef`（权威成绩位置）+
`inline`（完整 LabGradeResult 文档）接入；此前一律 `unavailable` + 原因。
`LabReportBuilder::validate` 拒绝没有 `gradingRef` 的 recorded 成绩。

### 秘密卫生

- 记录侧：run 环境经允许清单 + 拒绝清单；`extra` 注入前逐条 `redactSecretKeys`。
- 导出侧：所有嵌入文档（参数、结果、轨迹、workflow 证据、环境）再过一次
  `redactSecretKeys`（防御纵深，#789）。
- 测试：`tests/test_lab_report.cpp` 种入假 token，断言三种渲染都不含该串。

## 测试

`ctest -R lab_report -j1`（`tests/test_lab_report.cpp`）覆盖：schema 完整性、
确定性（两构建字节相等）、跨格式字段 parity、种入 token 的秘密过滤、回放级别
（无 pins → impossible + blockers 明列；接线齐全 → exact）、缩略图越界拒绝（builder
解析 PNG IHDR，不信任调用方声明）、成绩缝、归属策略、lineage 退化、三文件落盘、
真实 coordinator 端到端（自动登记 / opt-out 零记录 / 轨迹证据脱敏 / 报告闭环）。

相关：`docs/experiments/auto-recording.md`（ADR 0143 的用户视角）、
`docs/adr/0143-workflow-experiment-auto-recording.md`、`docs/adr/0146-lab-report-schema.md`。
