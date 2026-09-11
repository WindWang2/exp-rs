# OVERLAP MAP — 与并行 9.0 波次的冲突规避

## 活跃并行方向（2026-09-12）

| 方向 | PR/分支 | 核心目录 | 触碰 src/app? | 风险 |
|---|---|---|---|---|
| scientific-algorithms-9 | #883 | `src/scientific/**` | 否 | 低 |
| model-runtime-multimodal-9 | #884 | `src/models/**` | 否 | 低 |
| spatial-scientist-harness-9 | #885 | `src/agent/**`（Pi harness） | 否 | 低 |
| scientific-mlops-9 | #886 | `src/data/**`（versions/replay） | 否 | 中：M6 枚举要读 dataset/experiment 元数据 → 只经既有 store 接口 |
| execution-concurrency-lifecycle-9 | 本地 wt | scheduler/exec 生命周期 | 观察 | 中：TaskCenter 语义若变，TaskPanelHost 谓词需跟随 — 用注入谓词隔离 |
| geospatial-data-fabric-9 | 本地 wt | `src/geospatial/**` | 观察 | 低：I/O 经稳定 reader seam |

## 已识别的具体交叠点与对策

1. **TaskCenter / GuiJobAdapter**（execution-9 在改执行生命周期）：
   本方向 M1/M5 只消费 `TaskCenter` 既有公开信号 + 8.0 已注入的
   `hasInFlightTask` 谓词；若 rebase 时签名变化，适配点收敛在
   `src/app/shell/task_panel_host.*` 与 `gui_job_adapter.*`。
2. **Dataset/Experiment stores**（mlops-9 加 data versions）：
   M6 SchemaEnumProvider 只调用既有只读列举接口；不新建第二个元数据缓存。
   若 mlops-9 增加新列举 API，等其合入后以 adapter 接入，不抢写。
3. **Plugin platform**（8.0 已合入，无活跃 9.0 分支）：
   M8 的 UI placement 属于 `src/app/plugin_shell_ui.*`（本方向所有权），
   worker/protocol 属于已合入 master 的 plugin host —— 只消费不修改。
4. **共享文件**：`CMakeLists.txt`（tests/ 与 src/app/ 子目录）、`CHANGELOG.md`。
   冲突窗口最小化：milestone 末尾一次性追加；rebase 时若冲突，取双方并集语义。

## Rebase 策略

- 每 milestone 结束 rebase 一次 `origin/master`；
- 冲突只可能出现在共享文件与 `src/app/**`（8.0 系列 fix 已合，9.0 波次无其他人写
  src/app，若发现例外 → 停止重写，改经稳定接口集成并记录在本文件）。
