# PERFORMANCE — 资源模型与实测（Execution Runtime 11.0）

原则（GOAL envelope）：不用 wall-clock 当 correctness gate；规模证据使用内存上限、操作数/队列上限、逻辑规模和可复现 invariant。

## 资源模型（声明式上界，均有测试）

| 资源 | 上界来源 | 上界表达式 | 测试 |
|---|---|---|---|
| ChunkPipeline 峰值内存 | memory_planner（11.0 上界模型） | `((S+1)·cap·I + (S+2)·I + 1) · perTile + globalState`，S=stages、I=join 宽度、cap=queueCapacity | test_execution_governor_11（随机调度模拟 ≤ 公式） |
| ResumableTileRun 内存 | 驱动器设计 | O(1 tile payload + committed 位图 N/8 字节)；journal 流式重放 | test_execution_scale_fault_11（10^6 逻辑 tile 纯算术） |
| 队列容量 | BoundedChunkQueue | 构造时固定 cap（默认 2），FIFO，阻塞式 | test_chunk_graph（既有） |
| scratch 字节 | ScratchRegistry 预算 | acquire 前记账，超限 typed 拒绝（溢出安全 saturating） | test_execution_governor_11 |
| 在途写字节 | BoundedWriteGate | `maxInFlightBytes`；空载时超大单写放行（never-starve） | test_execution_governor_11（阻塞-解除验证） |
| 遥测内存 | ExecutionTelemetry | ring 8192 事件（溢出丢最旧 1/4）+ detail ≤512B；采样发射 ≤ tiles/rate+O(1) | test_execution_telemetry_11 |
| trace 内存 | FileTraceSink | 8MiB×4 文件轮转 + 8192 深度丢旧队列 | 既有 test_trace_contract |
| journal 磁盘 | ResumableTileRun | 每提交 tile ~24B 行；100k tiles ≈ 2.4MB（opt-in 实测往返） | test_execution_scale_fault_11 |

## 逻辑规模证据（本地，2026-09-15）

- 10^6 逻辑 tile 计划：`tileSpecAt(999999)` O(1) 解析 + digest 稳定（无网格物化）— test_execution_scale_fault_11。
- 100k 真实 tile journal 往返（opt-in SICNU_SCALE_11=1）：37,000 提交后硬停→恢复，`tilesReused ≥ 37,000`，kernel 调用 < total+1000 — 精确计数不变量，非墙钟。
- 默认日 gate：10^4 以内真实执行（bounded logical scale）。

## 构建/运行资源记录（本机 Windows, MSVC/Ninja -j2）

- configure（dev-default preset）：163.2s + 36.3s generating（Catch2 经 FETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src 离线提供）。
- sicnu_runtime 重构：9/9 目标，~1-2 分钟量级。
- 全量测试目标锥（qgis_core 等）：进行中（将记录到 EVIDENCE）。
- RSS/load 监测：Git Bash 无 loadavg → not-executed（按 GOAL 规则记录一次），build 恒 -j2；RSS 抽查 `tasklist`（ninja ≈ 91MB，cl.exe×2 峰值记录于 EVIDENCE）。

## 遥测开销

- 禁用态：每 record 一次 relaxed atomic load；计数器为 relaxed fetch_add（恒生效）。
- 启用态：采样率 16 时 200-tile 流水线产生 ≤ 4·(200/16+2)+8 ≈ 64 事件（实测断言）。
