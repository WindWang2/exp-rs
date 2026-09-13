# MILESTONES — large-scale-execution-engine-10

| # | 里程碑 | 内容 | 状态 |
|---|---|---|---|
| M0 | 基线落盘 | Phase 0 全文件 + 首次 commit | DONE |
| M1 | 能力契约 | memoryPolicy 新等级 + streamingHaloPixels + 投影 + 测试 | DONE |
| M2 | ChunkGraph | 多输入 tile DAG + join/backpressure + fan-out 守卫 + 测试 | DONE |
| M3 | Memory Planner | working-set 规划 + preflight tilePlan 接线 + actionable refusal | DONE |
| M4 | 外存层 | ScratchRegistry + disk tile store + write gate + multi-pass helper | DONE |
| M5 | Tile checkpoint | 长任务 checkpoint/resume + crash restart 测试 | DONE |
| M6 | 调度对接 | NVML→vram（opt-in seam）、fingerprint env pins v3、毒任务收敛钉测、#971 取消注入 | DONE |
| M7 | Scale/failure 测试族 | 10^6 tile、bounded scratch、cache 规模、毒任务收敛（crash storm 由 worker_host/ep9 承接） | DONE |
| M8 | Review 清零 + 最终验证 + PR | 2 subagents、P0/P1=0（含复验 F-M-1）、final HEAD 证据、PR | DONE |
