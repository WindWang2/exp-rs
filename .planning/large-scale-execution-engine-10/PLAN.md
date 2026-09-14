# PLAN — large-scale-execution-engine-10

预算包线 300M tokens（计量代理指标：时间戳/工具调用/触文件，见 EVIDENCE.md 预算节）。
九阶段占比按 goal-template 默认：P0 6% · P1 16% · P2 18% · P3 15% · P4 13% · P5 11% · P6 8% · P7 7% · P8 6%。

## 工作包（映射 CAPABILITY_MATRIX 缺口）

| WP | 内容 | 关键交付 | 阶段 |
|---|---|---|---|
| WP-0 | 基线/考古/去重/规划落盘 | 本目录全部 Phase 0 文件 + 首次 commit | P0 |
| WP-A | 执行架构基线审计 | A 审计结论写入 ARCHITECTURE.md；回归守护清单在位；缺口确认 | P0/P1 |
| WP-B | 能力契约（tileDependency/halo 声明） | rs_operator.h 契约扩展 + schema/metadata/agent capability 投影 + 测试 | P1 |
| WP-C | Tile DAG / ChunkGraph | tile_spec 扩展（bandRange/timeIndex）、ChunkGraph（N:1 join、确定性分区、tile lifetime）、ChunkPipeline 成为特例、无死锁测试 | P2 |
| WP-D | Memory Planner | tile working-set 规划器（input window+output+halo+stages+queueCap）、concurrency 降档、actionable refusal、admission 接线、TaskCenter 集成 | P2 |
| WP-E | Scratch / 外存 primitives | ScratchRegistry（登记/预算/引用/清扫/原子 finalize）、disk-backed tile intermediate、multi-pass reduction helper、disk writer 有界队列 | P3 |
| WP-F | 长任务 tile checkpoint | TileCheckpointWriter（版本/原子/身份门/漂移拒绝）、resume 集成、crash restart 测试 | P3 |
| WP-G | 调度对接：NVML→vram 准入、scratch 准入、毒任务隔离 | budget vram 维度吃 device truth、poison task 升级路径、worker crash storm 语义 | P3 |
| WP-H | cache env pins + #971 取消注入 | fingerprint environment pin（additive）、detection decode 取消点 | P4 |
| WP-I | Scale/failure 测试族 | test_large_scale_execution_10：worker crash storm、10^6 逻辑 tile、bounded scratch、cache 规模、checkpoint restart、饥饿/老化、wide fan-out join | P5 |
| WP-J | Review + 修复 | 2 subagents、P0/P1 清零、P2 尽量 | P6/P7 |
| WP-K | 收尾 | 最终 HEAD 验证、docs、CHANGELOG、rebase、push、PR | P8 |

## 执行顺序

P0（WP-0/A）→ P1（WP-B）→ P2（WP-C→WP-D）→ P3（WP-E→WP-F→WP-G）→ P4（WP-H + 集成核查 CLI/MCP/agent 投影）→ P5（WP-I）→ P6/P7（WP-J）→ P8（WP-K）。

每 Phase 完成即 commit + `git fetch origin && git rebase origin/master`（冲突时窄解，重跑受影响测试）。

## 完成判据（可核查）

1. `ctest -R "test_chunk_graph|test_large_scale_execution_10|test_execution_plane" -j1` 全绿（worktree build-dev）。
2. 新增 ADR 0148 在 `docs/adr/` 且被 CONTEXT.md 索引节引用。
3. `grep -R "tileDependency" src/operators/framework/rs_operator.h` 命中且 schema 投影测试在位。
4. Scale test 断言复杂度（逻辑 tile 数门控，SICNU_LSEE10_STRESS）。
5. P0/P1 = 0（REVIEW_LOG.md 全 finding 有 disposition）。
6. PR 创建（不 merge），body 含 local-evidence-only 声明。
