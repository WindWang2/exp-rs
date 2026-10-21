# R18-A Baseline — 遥感科学算法：数值正确性、质量契约与吞吐性能

- 记录时刻：2026-10-19（LIVE，非任务书快照）
- `origin/master` = `2d2881631fc821537100c00da677888e3a31ea8f`（`git fetch origin --prune` 实测）
- 本地 master checkout（只读宿主）= `46791bc65c6f`（落后 149 commits，仅作 Recon 宿主，不编码）
- worktree：`/home/kevin/project/exp-rs-r18-a-science`，分支 `agent/r18-a-science-numerics-perf`，基点 `origin/master`
- GitHub auth：`gh auth status` OK（WindWang2，push/PR 权限在）

## 开放 PR / Issue 状态（实时查询）

- 开放 PR 仅 1 个：**#1513**（draft，`hardening/r15-B-geodata-persistence`，68 文件写集：
  `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/cli/**`、
  `tests/CMakeLists.txt` 等）→ **全部禁区**。注意其触碰 `tests/CMakeLists.txt`，本轨若需改共享
  CMake 注册须先复查其 diff。
- 开放 Issue 2 个：
  - **#1491**（CI 无完成结果）→ 本轨不等线上 CI，仅记录。
  - **#1467**（core-fork local-modification cluster）→ 已有前人核验评论：4 项已修复、
    2 项明确搁置（spatial_split CMake 归属 = Windows 链接失败已撤销；bufferedBlockFolds 合并 =
    设计裁决留给 classification 域）。**本轨不接管**，见 CROSS_TRACK_FINDINGS.md。
- 无任何 `r18-*` 远端分支（`git branch -r | grep r18` 空）→ R18-B/C/D/E 尚未推送，无并行竞争。

## 前任轨道写集（避免重复/冲突）

- #1514（r15-A，已合并 2026-10-18 07:59Z）：`image_fusion.cpp`、`rs_sam_classify_operator.cpp`、
  `rs_multires_segmenter.cpp` + 3 个测试文件。**关键发现：其生产修复被 #1516 合并静默回滚**
  （见 TASKS.md WP1，本轨首要工作包 = 恢复而非重造）。
- #1506（r14-A）、#1483（r12-a）、#1479（r11-a）：历史科学算法轨道，产出 sentinel wash、
  权重补齐、NaN 语义等现行代码。

## 机器与资源基线

- Linux x86_64，40 核（不用，按仓库纪律 -j2 封顶），62 GiB RAM（可用 ~51 GiB）
- 构建树：`build-r18-a`（Release + ENABLE_TESTS=ON，镜像 ci-fast preset 的 cache 变量）
- 全局五轨锁：`/tmp/exp-rs-five-tracks-build.lock`（flock）
- 环境变量：`SICNU_BUILD_REQUIRE_GUARD=1`、`SICNU_BUILD_JOBS_CAP=2`、
  `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`

## Recon 三份只读审计（摘要，全文见各工作包引用）

1. `src/processing/algorithms/`：头条 = #1514 回滚（P1）；±Inf 未洗入融合产品（P2）；
   内存版 dos1/dos2 -Inf → 全 +Inf 波段（P2）；linearWeighted 缺指针防护（P3）等。
2. `src/analysis/`：多分辨率分割器每像素 2 次堆分配（4096²×10band >2.6GB，P1 内存）；
   `cv::setRNGSeed(42)` 进程全局副作用（P2）；PCA-difference 单 NaN 毒化全图（P2）等。
3. `src/operators/rs/`：`rs_raster_spatial_operators.cpp` band 参数 `std::max(1,…)` 无上界校验、
   band=0 静默改 1（P2）；SAR 地形对 DEM 仅查 dims（P2）；SAR terrain 无取消钩子（P2）等。

审计由 3 个独立只读 Explore 子代理完成（未参与后续实现，可用作 R1 reviewer 候选隔离基础）。

## 基线漂移记录（交付前 LIVE 复查，2026-10-19 深夜）

- 交付时 `git fetch --prune` 后 `origin/master` = `e12a9afbc`（较本轨基点 2d2881631
  前进 145 提交：含 r18-d、#1662 fix/full-review-sweep 等）。
- 交集核查：本轨 17 个改动文件与新 master 改动交集仅 2 个——
  `rs_mosaic_operator.cpp`（试验合并**自动合并成功**，双方 hunk 不相交，isfinite 修复存活）
  与 `.gitignore`（双方 EOF 追加块 → **唯一冲突点**，一行级琐碎冲突，与 R2 P3-2 预测一致）。
- 新 master 的 6942ed100（Inf 治理 GREEN wave）覆盖的是**不同文件集**
  （simple_segmenter/segment_features/pixel_ignore/fast_6s/sieve/kmeans RNG），与本轨
  互补无重复；其 kmeans RNG per-fit 重播种正是本轨 WP8b 缓办项——已由该提交处理，
  缓办决策与实时查重一致。
- 处置：按任务书 §8.3，PR 记 needs-rebase（.gitignore 一处），不假装零冲突；
  未做 rebase+全量重测（避免在上下文尾部半途而废），交由维护者裁定。
