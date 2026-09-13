# MILESTONES — large-scale-execution-engine-10

| # | 里程碑 | 内容 | 状态 |
|---|---|---|---|
| M0 | 基线落盘 | Phase 0 全文件 + 首次 commit | DONE |
| M1 | 能力契约 | tileDependency/halo 声明 + 投影 + 测试 | PENDING |
| M2 | ChunkGraph | 多输入 tile DAG + join/backpressure + 测试 | PENDING |
| M3 | Memory Planner | working-set 规划 + admission 接线 + actionable refusal | PENDING |
| M4 | 外存层 | ScratchRegistry + disk tile store + writer 节流 + multi-pass helper | PENDING |
| M5 | Tile checkpoint | 长任务 checkpoint/resume + crash restart | PENDING |
| M6 | 调度对接 | NVML→vram、scratch 准入、毒任务、cache env pins、#971 取消注入 | PENDING |
| M7 | Scale/failure 测试族 | 10^6 tile、crash storm、bounded scratch、cache 规模 | PENDING |
| M8 | Review 清零 + 最终验证 + PR | 2 subagents、P0/P1=0、final HEAD 证据、PR | PENDING |
