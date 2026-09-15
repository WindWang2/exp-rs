# PLAN — F15 mosaic-fusion-11

基线：`origin/master@a5b11b7f10`。分支 `zcode/mosaic-fusion-11`，worktree `../exp-rs-mosaic-fusion-11`。

| Phase | 交付 | 关键文件 | 验证 gate |
|---|---|---|---|
| 0 | 审计+规划落盘（本文档族） | `.planning/mosaic-fusion-11/*` | commit + `git status` clean |
| 1 | 契约/数据模型：mosaic_plan（A 包核心类型 + inventory/grid plan/overlap 图）+ 测试 | `mosaic_plan.{h,cpp}`, `test_mosaic_plan`, CMake 追加 | `ctest -R test_mosaic_plan -j1` |
| 2 | B+C：mosaic_balancing、mosaic_seamline + 已知答案/负测试 | `mosaic_balancing.*`, `mosaic_seamline.*`, 各自 test | `ctest -R "mosaic_balancing|mosaic_seamline" -j1` |
| 3 | D+E+F：mosaic_blend、mosaic_quality、fusion_quality_report + tests | 3× 新模块 + tests | `ctest -R "mosaic_blend|mosaic_quality|fusion_quality_report" -j1` |
| 4 | surface：`rs:quality_mosaic` 算子 + `rs:image_fusion` 扩展 + 注册/contract/capability/CHANGELOG/ADR/docs | operator 文件 + 5 个 integration files + docs | `ctest -R test_quality_mosaic_operator -j1`；注册 drift 检查 |
| 5 | 硬化：scale/内存上限证据、cancel、失败清理、Unicode/read-only 路径 | `test_mosaic_scale`、operator 边界 | scale gate + resource log |
| 6 | E2E/drift：多场景 known-answer 全链路、provenance 追溯、sidecar 原子性、能力目录一致性 | operator E2E + drift 断言 | `ctest -R "mosaic|fusion" -j1` 全绿 |
| 7 | 独立 adversarial review（subagent #2）+ P0/P1 remediation | REVIEW_LOG.md | P0=0 P1=0 + 重跑相关 gate |
| 8 | 双验证、rebase、diff/secret 扫描、PR_BODY、push、PR | EVIDENCE.md | Oracle 1–7 全过 |

## 风险与对策

- **全量构建时间**：只 build 触达的 target（sicnu_processing / sicnu_operators / 具体 test targets），不跑全量 `all`。
- **tests/CMakeLists.txt 与 PR #1008 冲突**：追加集中在单处、一次 commit。
- **seamline 正确性**：DP 路径核心做成纯函数（成本面 → 路径），已知答案独立手算；N 场景合成层单测。
- **provenance 追溯 Oracle**：E2E 用两个已知 offset 场景验证 provenance 波段 == 场景索引映射。

## Ledger

`.goal-loop-ledger.md` 从 Phase 1 起逐轮记账。
