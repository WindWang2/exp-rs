# BASELINE — R28-T02 数据与执行运行时

## 调查时刻事实

- 调查 UTC 时间：2026-11-04T18:04Z（本机 +0800 为 11-05 02:04）
- `origin/master` = **50cc3255c5eb30cd247d7c065026607f36f90f03**（= 任务包快照基线，master 未移动）
  - merge commit of PR #1812（swarm review 131 findings fix），merged 2026-11-04T15:16Z
- 工具链：CachyOS linux 6.18.55-2-cachyos-lts x64；16 核 / 62GB RAM（41GB 可用）；swap 62GB；/home 余 681GB
- gh 认证：WindWang2（token 具 repo/workflow 等 scope）
- 本 worktree：`/home/kevin/projects/rs-studio/exp-rs-r28-t02-20261104T180429Z`，分支 `agent/r28-t02-data-runtime-20261104T180429Z`，从 BASE_SHA 全新创建
- 用户主工作区 `main` 在 `fix/ui-review-2026-11-05` 分支（= open PR #1813 的 head，UI 域，2 commits ahead of master）+ 未提交修改（translations/tests）——只读，不触碰

## 并发/锁协议事实（本次实测）

- `~/.cache/exp-rs-agent/git-metadata.lock` 实际是一个 **0 字节 flock 文件**（02:03:34 创建，疑似 r28-t01 挂起的残留；flock 可立即获取=无人持有）。本轨采用 `flock -w <timeout> 9` on 该文件 串行化 fetch/worktree add，不再用 mkdir 目录锁（与文件锁路径冲突导致此前 30 分钟假死锁排查）。
- 构建锁：HOST_LOCK=`$HOME/.cache/exp-rs-agent/host-build`（若为目录锁则配合 resource_guard.py；实测后更新本节）。

## 前作与本轨起点

- **#1810**（r27-T2，merged 2026-11-03T16:20Z, merge commit 3dd54c6c12）：本轨直系前作。已落地 `src/data/import_session.h`（check/proceed/cancel/resume + durable journal，schema sicnu.import.session/1）、`geo::remote::safeRemoteRasterPath`、`ResumableTileRun` OS 锁、JobEngine 有界 shutdown（detach+immortal singleton+deadline）、worker 租约表有界化。自称修复 #1743/#1764/#1765/#1766/#1767/#1803，但 **这 6 个 issue 在 GitHub 上仍是 open**（PR body 未用 Fixes 关键字）→ 本轨需实证核验后在自家 PR 中正式清偿或转交。
- **#1812**（merged 2026-11-04T15:16Z）：131 swarm findings 全修；终态记录 fast CTest 9776 中 **77 失败**，其中明确点名遗留：
  - **TaskCenter/ExecutionPlane cancellation cluster**（本轨 WP01 首要目标）
  - surface/contract snapshot drift（部分已在 #1812 修复；其余需 OpenCV 工具链机器再生）
  - cartography atlas deliveries（master 前已有）
  - R25/R26/R27 语义变化导致的单测期望漂移尾部
  #1812 issue comments 为 0；无更细的失败清单——本轨必须自己复现取得。
- r25-B（#1726）落地 ImportPreflight 解释报告 / 可解释推理计划；r24_b（#1719）落地检测 NoData 哨兵 / tile 尾批丢弃 / dataset 事务门。

## 明确不在本轨基线内的事实

- Windows（LockFileEx/Job Object）分支仅有编译目标，未在 Windows 实测（#1810 声明，本轨同样 NOT_EXECUTED）
- GPU/ONNX 真实推理：待本轨 WP10 实测后据实标注
