# PR_BRANCH_MATRIX — R28-T02（调查时刻 2026-11-04T18:04Z, master=50cc3255c5）

## Open PR（全量，1 个）

| PR | 分支 | 状态 | 与本轨关系 |
|---|---|---|---|
| #1813 | fix/ui-review-2026-11-05（用户本人） | open, 2 commits ahead of master, 0 behind | UI 域（T04 相邻）。改动面=workbench UI + translations + 3 个 UI 测试。**无本轨文件重叠**；不抢改、不 cherry-pick。合并时若有 tests/CMakeLists.txt 冲突按轨号块追加原则解决 |

## Open PR 的 review 状态

- #1813 无 review comments（gh api pulls/1813/reviews 为空数组——截至调查时刻）。"无 review" ≠ 已批准。

## 远端分支 vs master（ahead/behind，全部实测）

- agent/r23-* ×5、r24_* ×3、r25-* ×5、r26-* ×3、r27-t0* ×5：**全部 0 ahead**（已全量合入 master），无未推工作。
- fix/swarm-review-2026-11-04：0 ahead（#1812 已合）。
- fix/ui-review-2026-11-05：**2 ahead, 0 behind**（= #1813）。
- **无任何 agent/r28-* 远端分支**：R28-T01（本地 worktree exp-rs-r28-t01-science-20261104T180334Z 存在，HEAD=50cc3255）尚未推送——同机活跃并行 agent，science 域（T01）。预期共享热点=tests/CMakeLists.txt / 根 CMake：只做最小轨号标记追加。
- R28-T03/T04/T05：调查时刻未见本地 worktree 也无远端分支；可能后续启动（未知）。其他机器工作不可见，标注为 UNKNOWN。

## Closed PR（读过的与本轨相关）

- #1810 r27-T2（本轨直系前作，merged）：import_session / safeRemoteRasterPath / 有界 shutdown / 租约有界。详见 BASELINE.md。
- #1812 swarm-review-131（merged）：终态 77/9776 fast CTest 失败清单含 TaskCenter/ExecutionPlane cancellation cluster → WP01 目标。
- #1726 r25-B（merged）：ImportPreflight / 推理计划可解释性。
- #1719 r24_b（merged）：检测 NoData 哨兵 / 尾批 tile / dataset 事务门。
- 最近 607 个 closed PR 中 merged=607+（采样核对 r25/r26/r27 全 merged；closed≠merged 区分已完成——closed 列表里非 merged 的 33 个为陈旧草稿，与本轨路径无关）。

## 避重结论

1. 本轨与 #1813 零文件重叠（其 touched files 为 src/app UI、resources/translations、tests/test_*_panel/dock/preferences——均 T04 域）。
2. R28-T01 同机并行：我只新增 `tests/test_r28_t02_*`、`docs/development/r28/t02/*` 唯一命名文件；tests/CMakeLists.txt 只追加带轨号最小注册块。
3. r27-t02 分支已全量合入——**没有他人未合入的本轨历史工作可撞**。
