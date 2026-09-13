# PERFORMANCE — large-scale-execution-engine-10

原则：性能数字是 evidence 不是 gate；热点先 profile 再优化。

## 基线（master @ 7d78059d1a，not-executed 于本机时标注）

- READINESS.md（@ f0f6869b27）：`job_dispatch_roundtrip` 4.50M ops/s；`schedule_1000` 69k ops/s（历史 quick-tier 证据，非本机本轮）。
- 本 track 目标证据（Phase 5 采集）：
  - ChunkGraph join 吞吐（合成 tile，1e5-1e6 逻辑 tile）
  - scratch 落盘吞吐与有界写队列的尾部延迟
  - planner 开销（每次提交的估算成本，应 O(1)/O(dims)）
  - TaskCenter admission 在 100k 任务下的每 pass 成本（回归 ep9 证据）
