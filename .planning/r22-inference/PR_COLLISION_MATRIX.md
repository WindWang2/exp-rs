# R22-03 PR Collision Matrix

基线 911e0b17；每次远端变动后重评（push 前强制复查一次）。

## Push 前复查（2026-10-26）

- 远端 master 仍为 `911e0b17`（gh api 复测），无新 merge。
- #1673 head `941af0d18` 自初查未变。
- 新增 open PR **#1708**（r22-infra 轨，head bbdd034f9）：文件交集仅 `.gitignore`（双方均为尾部 append，语义不同的白名单段，git 可能文本冲突但语义无碰撞——合并者保留两段即可）与 `tests/CMakeLists.txt`（其注册块位置与本轨不重叠，可自动合并）。**无 src 文件交集。**

## 与 open PR 的文件交集

| PR | 交集文件 | 冲突风险 | 处置 |
|----|---------|---------|------|
| #1673 (r21-B, open) | `src/operators/runtime/tile_inference_engine.cpp` | **中**：同文件不同 hunk。#1673 改 head-role threshold 门控（~:493 新函数、:1726/:3779 附近、:1840/:3839 门控行）；本轨改 FeatherAccumulator（:701-981 类体 + 调用点 :1839/:1881/:2015 的语义由类内吸收，尽量不改调用行）。两侧 hunk 不重叠，git 可自动合并；语义无交集（thresholding vs blending）。 | push 前 fetch #1673 head 复核；PR body 注明 |
| #1673 (r21-B, open) | `tests/CMakeLists.txt` | **低**：#1673 在文件尾部追加 r21-b 测试注册；本轨在 `test_model_runtime_9` 块后精确插入新目标，位置不同，append-only。 | 同上 |
| #1673 (r21-B, open) | `tests/test_model_runtime.cpp` / `test_model_runtime_9.cpp` | **无**：本轨不改这两个文件（新测试放 `tests/test_r22_inference_feather.cpp`）。 | — |
| #1674 (r21-E) | 无文件交集 | 无 | — |
| #1675 (r21-C) | 无文件交集 | 无 | — |
| #1676 (r21-A) | 无文件交集 | 无 | — |
| #1677 (r21-D) | 无文件交集 | 无 | — |

## 与已合并历史 PR 的核对

- #1669（MERGED，在基线内）：改过 `tile_inference_engine.cpp`（batching/uncertainty/contract）。本轨基于含它的 911e0b17 开发，无 merge-loss。
- #1659（MERGED，在基线内）：feather 滑窗修复（R18-B slide corruption）、chunk_graph。已在基线。
- #1508（MERGED，在基线内）：检测/分块加固。已在基线。
- merge-loss 防护：本轨 worktree 从 `911e0b17`（= origin/master 实测）创建，包含全部已合并 R20 成果。

## 接口符号 / 测试族冲突

- 本轨新增测试目标 `test_r22_inference_feather`（全仓库唯一名，`grep -r test_r22_inference` 无既有目标）。
- 不新增导出符号；FeatherAccumulator 是 tile_inference_engine.cpp 匿名实现细节（无外部 ABI 影响）。
