# BASELINE — R29-T02

- 快照日期：2026-11-09（北京时间 2026-11-08 深夜建立 worktree）
- base SHA：`130990a39655338da3790f0aab0bea38a2fb7d20`（origin/master，与任务文件快照一致）
- 最后合并 PR：#1826（fix/pr-review-followups-2026-11-06）
- open PR：0（`gh api pulls?state=open` → `[]`）
- 远程分支：R28 各轨分支已全部保留（agent/r28-t01..t05），无 R29 分支（本轨首建）。
- 本机 worktree：main（fix/pr-review-followups-2026-11-06，已合并 #1826，只读参考）+ 6 个 R28 worktree（不动）。

## 关键源码事实（本 HEAD 实读）

### D-T05-CHAIN1（published marker 假绿）
- `src/runtime/chunk/resumable_tile_run.cpp:208-218` `markerMatches()` 只验证 marker 文件内的 `version + identityKey`。
- `:301-305` `execute()` 在 marker 命中时直接 `alreadyPublished=true` 返回，零 compute/consume/publish，不验证声明输出存在。
- `src/operators/framework/chunked_run.cpp:102-110` `runResumable`：workDir 空 → `temp_directory_path()`（即 /tmp/sicnu_operators），stateBase=`<workDir>/chunked-<identityKey>`。
- `src/jobs/job_engine.cpp:1104` `sicnu::operators::RSOperatorContext ctx;` 默认构造 → `rs_operator_context.cpp:16-20` 空 workDir → `/tmp/sicnu_operators`（跨进程/工作区共享）。
- `src/operators/rs/rs_spectral_index_operator.cpp:645-673` canonicalParams **已含** input/output/index/bands/postfire → 身份键已含输出路径（历史 handoff 的"输出路径没进身份"已过时）。**剩余缺陷 = marker 不验证产物现状**：同目标路径产物被删 → 假绿；产物被替换 → 假绿。
- `src/operators/rs/rs_change_streaming.cpp:769-822` alreadyPublished 时重开输出 + `SICNU_CHANGE_IDENTITY` 元数据校验，缺失/不匹配 → typed error（fail-closed 参考）。
- `src/cli/rs_pipeline_runner.cpp:893` `registerOutputAsset` 输出不可打开时只 warning，step 仍 Completed → CLI 假绿出口。

### 虚拟栅格 preflight vs provider
- `src/data/virtual_raster_preflight.cpp:168-178` 只比较**输入之间** CRS；`:241-254` RequiresReprojection 仅由 inter-input 差异触发。**缺口**：输入彼此同 CRS 但显式 targetCrs 不同 → 不触发，落 Compatible。
- `src/data/providers/virtual_raster_source_provider.cpp:94-95,160-161` targetCrs 直接写入 `<SRS>`；`:235-272` SrcRect/DstRect 用**输入自身 CRS** 的 geotransform 计算 → target≠input CRS 时仅换标签，不做 warp（空间错误）。
- 旋转/剪切（gt[2]/gt[4]≠0）：SrcRect/DstRect 为轴对齐矩形，无法表达旋转源的正确像元位置。
- 真实 warp seam 已存在：`src/geospatial/convert/raster_convert.cpp` `warpRaster()`（GDALWarp，target CRS/resolution/bounds/nodata 契约，staging+验证+原子发布）。

### H1 制图线程
- `src/agent/cartography/cartography_operators.cpp:49` `requireQgisHost()` 只查 QgsApplication 存在，不查线程。
- `src/jobs/job_engine.cpp:660-680` detached std::thread worker 池；`1340-1353` executor/op 在 worker 线程调用。
- `src/processing/framework/task_center.cpp:4542` `waitForTask` 主线程 park 在 `m_waitCondition.wait`，不处理事件 → worker 侧 BlockingQueuedConnection 会死锁（handoff 明令禁止）。
- CLI 已在 `rs_pipeline_runner.cpp:643-647,1127-1154` waitForPipeline 轮询间隙 `processEvents()` → 队列投递可被服务。

### R28 交接
- r28-t02/HANDOFF：ImportSession stepwise API、typed remote errors 稳定；`geo::fabricCachedPath` 未装 cache 时 typed 拒绝。
- r28-t04/HANDOFF H1：engine/operator 表面字节一致（同线程），TaskCenter worker 表面全透明 PNG（mean alpha 0.0）；`SICNU_CP11_THREAD_PARITY=1` 为严格 oracle。
- r28-t05 ISSUE_TRIAGE：D-T05-CHAIN1 完整调用链根因（见上）。

## Issue 状态（本轨相关，2026-11-09 实查）

| Issue | 标题 | 状态 |
|---|---|---|
| #1803 | [P3][runtime-data-io] 低优先级汇总 | 汇总桶，按子项核对 |
| #1767 | RIO-8 无超时 shutdown 无界 join | PARTIAL（#1767 watchdog 已见于 job_engine timeoutMs 路径；细节未核） |
| #1766 | RIO-5 无独占锁并发互毁 | FIXED_OPEN（resumable_tile_run R7 ExclusivePathLock 已在；issue 未关） |
| #1743 | RIO-1 addJoin({A,A}) 绕过单消费者 | 未核（本轨 WP 范围外优先） |
| #1705 | JobEngine logLines 无界增长 | FIXED_OPEN（#1705 bounded eviction 已在 job_engine.cpp appendLog） |

## 构建环境

- preset `dev-default`（Debug, tests ON）+ `-DOpenCV_DIR=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib/cmake/opencv5`（与 R28 一致）。
- 双锁：host `${XDG_CACHE_HOME:-~/.cache}/exp-rs-agent/host-build` + build `build-dev`，`--parallel 4 --max-parallel 4`。
- configure EXIT=0（log-configure.txt）。
