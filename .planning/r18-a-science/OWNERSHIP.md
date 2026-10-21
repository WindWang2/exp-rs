# R18-A OWNERSHIP — 独占写区 / 只读区 / 禁区

依据：任务书 §1 写区规则 + 2026-10-19 实时 overlap_scan + #1513 实时文件清单。

## 独占写区（本轨提交的文件必须落在这些前缀内，除非有书面跨轨许可）

- `src/processing/algorithms/**`（传统科学算法内核）
- `src/analysis/**`（分割/分类/高光谱/几何配准）
- `src/operators/rs/**`（传统 rs_* 算子；**不含** rs_inference / rs_model_task_operators = R18-B）
- `tests/test_image_fusion.cpp`、`tests/test_perf_fusion.cpp`、`tests/test_temporal_algorithms.cpp`
  等本轨已有测试文件；新增 `tests/test_r18_a_*.cpp`
- `benchmarks/**` 本轨独占子文件（先查 overlap）
- `src/processing/gdal/**` 算法独有适配（小范围，先查 overlap）
- `.planning/r18-a-science/**`

## 只读区（发现跨轨问题 → CROSS_TRACK_FINDINGS.md，不顺手修）

- `src/operators/runtime/**`、`src/runtime/gpu/**`、`pi/**`、`src/agent/**`、`src/workflow/**`、
  `src/app/**`、`src/teaching_admin/**`、`src/verification/**`
- `src/core/temporal_cube.cpp`、`radiometric_state.cpp`（#1467 相关，前人已核验）
- `rs:infer/segment/detect/embedding` 模型推理与 provider（R18-B）——本轨仅经 operator interface 读取

## 禁区（#1513 写集，触碰即停）

`src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/cli/**`、
`tests/CMakeLists.txt`（#1513 也改了它 — 若必须改，仅限新增本轨 target 的最小独立块，
提交前 `git diff` 复查与 #1513 的行级交集并在 PR 标出）。

## 实时查重记录

- `overlap_scan.py src/processing/algorithms src/analysis src/operators/rs --json` →
  `overlap_count: 0`（open PRs / merged 近期 / remote branches / sibling worktrees 均无交集）
- `git branch -r | grep r18` → 空（R18-B/C/D/E 未推送）
- 兄弟 worktree（r16-a/b/c/e、hardening 等）基点均旧于本轨，无算法目录活跃写入。

## 基线变化时的复查规则

每次 rebase / master 前进后重跑 overlap_scan；若 #1513 或新 PR 开始写 `src/processing/algorithms`
或 `src/analysis`，停止冲突子项并在 ledger 记录。
