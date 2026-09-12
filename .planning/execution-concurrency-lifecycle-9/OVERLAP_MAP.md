# OVERLAP MAP — 并行开发冲突规避

本轮约 10 个大型并行 worktree（-9 系列）。基于 git worktree list 与 remote 分支核查：

## 冲突面分析

| 目录/文件 | 本方向 | 其他方向 | 缓解 |
|---|---|---|---|
| `src/processing/framework/task_center.*` | **拥有** | 前身 execution-plane-8 已合并；无活跃 -9 分支占用 | 无冲突 |
| `src/workflow/**` | **拥有** | 同上 | 无冲突 |
| `src/jobs/**` | **拥有** | 同上 | 无冲突 |
| `src/data/data_manager.*` | 窄 seam（M2 快照契约） | dataset-experiment 轨可能重构 store | 只加只读快照接口；不动 schema；milestone 末提交 |
| `src/app/**` | 禁改 | workbench-9 轨拥有 | #859/#861 划界 |
| `src/operators/**`、`src/agent/**` | 禁改 | scientific/harness/cartography 轨 | 无交集 |
| `tests/CMakeLists.txt` | 末尾最小增量 | 所有轨 | 追加行，冲突易解 |
| `CHANGELOG.md` | PR 前最小增量 | 所有轨 | 同上 |

## 远端分支残留判定

- `origin/feat/*-5/6/7/8`：全部对应已合并 PR（#818-#847），判定为历史残留，不作为活跃开发信号。
- 无 `-9` 前缀远端分支（截至 fetch 时间）。若开发期间出现其他 `-9` 分支修改本方向核心文件，
  以稳定接口集成为先，不双边重写同一 seam。

## 主工作区未提交草稿的处置

main worktree 有针对 #853-#881 的未提交草稿（非本方向产出）。本方向：
- 不引用、不提交、不丢弃这些草稿；
- 其中 execution 侧相关的两处（TaskCenter worker 宽限、coordinator recursive_mutex）
  经复验为真实问题，但按本方向根因方案重做（结构化准入 + 通知队列），不采纳 band-aid。
