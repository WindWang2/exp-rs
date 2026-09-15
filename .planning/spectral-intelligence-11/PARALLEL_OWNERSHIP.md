# PARALLEL_OWNERSHIP — spectral-intelligence-11

启动时（2026-09-15）open PR / remote branch 与本 track 的文件级交集与策略。

## 规则（GOAL 原文）

1. 仍开放 PR 的 changed files 默认 read-only，不得复制/重做其功能；
2. 依赖其新 API 时优先用 master 已有稳定 seam，否则降级为 adapter/contract/test scaffold 并在 PR_BODY 标 follow-up；
3. PR 已合并则以新 origin/master 重新审计；
4. 新出现的并发 PR 同样适用；
5. open issue 逐条 dedupe，已修复未关的记录证据。

## 矩阵

| 并行对象 | 状态 | 与本 track 交集 | 策略 |
|---|---|---|---|
| PR #1008 `zcode/radiometric-spectral-workbench`（branch 同名） | open, CONFLICTING with master | `src/processing/algorithms/spectral_unmixing.{h,cpp}`、`spectral_indices.{h,cpp}`、`src/app/widgets/spectral_profile_widget.{h,cpp}`、`src/core/spectral_library.{h,cpp}`、`tests/CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、`src/agent/CMakeLists.txt`、`src/core/CMakeLists.txt`、`.gitignore`、`tests/test_spectral_*.cpp` 若干 | **全部 read-only**。本 track 稀疏解混/相似度/RX/端元分析全部放**新文件**；GUI 放新 widget 文件；共享注册文件（tests/CMakeLists、app CMake、.gitignore）仅最小 append。不重做其 FCLS/VCA/continuum/指数/辐射校正。 |
| #991 D18 unified mission workbench | **已合并**（c5d4aafe） | D18 主工作台文件（src/app mission/workbench 相关） | 以合并后 master 为事实源；GOAL 明令不改 D18 主工作台文件 → 本 track GUI 面板为独立新文件 + 最小挂载。 |
| #992 D19 dataset foundry/benchmark | **已合并**（1cea9892） | 无光谱交集 | 无。 |
| issues #1001–#1007 | open | 0（全部 dataset/workflow/georef/io/agent 域） | dedupe 完成：非本 track 责任，不修。 |
| `ISSUES.md` 旧 D3 backlog | 文档 | H-1/H-2/H-3 已被 10.0 修复（`rs_library_select`、`rs_spectral_reference_input`、`rs_mnf_inverse`、`rs:endmember_extraction` 的 `endmembersOut` artifact 均在 master） | 不作为 backlog；仅线索。 |

## 本 track 主张的 primary write scope（启动审计后收窄）

新文件：
- `src/processing/algorithms/spectral_local_rx.{h,cpp}`（A）
- `src/processing/algorithms/spectral_sparse_unmixing.{h,cpp}`（B）
- `src/processing/algorithms/spectral_hybrid_similarity.{h,cpp}`（C）
- `src/processing/algorithms/endmember_analysis.{h,cpp}`（D）
- `src/operators/rs/rs_local_rx_operator.{h,cpp}`、`rs_sparse_unmixing_operator.{h,cpp}`、`rs_spectral_similarity_operator.{h,cpp}`、`rs_endmember_analysis_operator.{h,cpp}`（G）
- `src/app/spectral/spectral_workbench_*.{h,cpp}`（F，独立于 D18 与 #1008 widgets）
- `tests/test_spectral_local_rx.cpp`、`test_spectral_sparse_unmixing.cpp`、`test_spectral_hybrid_similarity.cpp`、`test_endmember_analysis.cpp`、`test_spectral_scale.cpp`（如不存在）等

共享文件最小 append-only（master 权威、#1008 亦改 tests/CMakeLists.txt → 冲突面控制）：
- `src/processing/CMakeLists.txt`（或算法所在 target 的清单——以 Phase 0 架构图为准）
- `src/operators/rs/rs_operators_init.cpp`（注册点追加；#1008 不触碰 operators）
- `tests/CMakeLists.txt`（新 test target 追加）
- `src/app/CMakeLists.txt`（新 widget 文件追加）
- `data/processing/algorithm_meta/...`、`data/agent/capabilities/spectral.json`（capability descriptor 追加）
- `CHANGELOG.md`、`docs/spectral/**`、`.planning/spectral-intelligence-11/**`

明确不碰：#1008 changed files 的业务内容；D18 主工作台文件；generic model runtime；`spectral_profile_widget.*`；`spectral_unmixing.*`；`spectral_indices.*`。
