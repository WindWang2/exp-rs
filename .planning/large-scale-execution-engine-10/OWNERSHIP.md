# OWNERSHIP — large-scale-execution-engine-10

## 本 Track 独占可写

- `src/runtime/chunk/**` — tile DAG / stream graph / 外存 primitive 扩展
- `src/runtime/worker/**` — worker 协议扩展（append-only，optional 字段/能力）
- `src/runtime/gpu/**` — 仅当 admission 对接需要窄改动
- `src/jobs/**` — JobEngine（队列上限/backpressure/毒任务计数）
- `src/processing/framework/` 内新增文件 + 以下文件的窄改动：
  - `task_center.{h,cpp}`（admission/memory planner 接线、checkpoint、scratch 预算）
  - `local_worker_pool.{h,cpp}`、`local_worker_host.*`（毒任务/健康）
  - `worker_execution_route.*`（路由维度）
  - `task_resource_budget2.h`（窄扩展，append-only 维度）
  - `resource_estimation.h`（tile working-set planner 复用其 overflow-safe helper）
  - `CMakeLists.txt` 条目（append-only）
- `src/data/execution_fingerprint.{h,cpp}` — 仅 append-only 扩展（environment pin 维度等）
- `src/workflow/workflow_checkpoint.*` — 仅 append-only（长任务 tile checkpoint 复用其原子写）
- `src/operators/framework/rs_operator.h` — 能力契约扩展（halo/global-reduction/external-memory 声明；默认值保持旧行为）
- `tests/test_large_scale_execution_10.cpp`（新）、`tests/test_chunk_graph.cpp`（追加用例）、相关 CMake
- `docs/adr/0148-*.md`（新 ADR）、`docs/architecture/`（如需）、`.planning/large-scale-execution-engine-10/`
- `CHANGELOG.md`（收尾条目）

## 只读 / 他方所有权（禁止业务级改动）

- `src/operators/rs/**`、`src/operators/opencv/**`、`src/operators/otb/**` — 科学算法内核（其他 track 所有权）。本 track 只允许在 rs_operator.h 契约层添加默认向后兼容的声明。
  - 例外（issue #971 的执行面部分）：`detection_decode` 的取消注入点属于"运行时有界计算/取消"ownership；若实施，只注入取消检查与有界工作集，不改 NMS 几何语义，单独 commit 并在 DECISIONS.md 记录。
- `src/geospatial/fabric/**` — PR #974（cloud-data-fabric-10）所有权
- `src/app/**`、`src/ui/**` — workbench/UI 轨所有权；本 track 不改 UI（执行面通过既有 TaskCenter 信号自然可见）
- `src/core/`、`src/gui/` — vendored QGIS
- `src/data/` 其余文件 — data plane 轨（DataManager 等只读；execution_fingerprint 白名单例外如上）
- `src/experiment/`、`src/dataset/` — MLOps 轨
- `src/sdk/**` — plugin/SDK 轨（IPC framing 已由 #901/#936 修复，只读）
- `pi/**` — Pi 桥
- `docs/processing/*.md` — 科学策略文档（其他轨）；本 track 的契约文档放 `docs/architecture/` 或 ADR

## 共享文件冲突表

| 文件 | 并行 Track | 冲突风险 | 缓解 |
|---|---|---|---|
| `src/processing/framework/task_center.cpp` | 10.0 各轨均可能碰 | 中 | 窄 hunk、独立 integration commit、每 Phase rebase |
| `.gitignore` | #970 过程缺陷 | 低 | append-only 白名单条目 |
| `CMakeLists.txt`（tests/src） | 全部 | 低 | append-only 条目 |
| `CHANGELOG.md` | 全部 | 低 | 收尾单条目追加 |
| `src/operators/framework/rs_operator.h` | capability-knowledge 轨（已合并） | 低 | 只增虚函数/默认实现 |

## Subagent 分配（Phase 7）

- Subagent A（只读）：架构 + 并发/生命周期正确性 review — task_center/job_engine/chunk DAG/checkpoint diff
- Subagent B（只读）：backpressure/内存界/测试可信度/性能 review — queue 容量论证、外部存 primitive 的复杂度断言、scale test 的 vacuous 断言清扫
