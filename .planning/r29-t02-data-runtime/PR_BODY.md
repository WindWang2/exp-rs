# R29-T02 — 数据、空间 I/O、任务执行与真实产物闭环

## 原问题与影响

三个已被证据锚定的用户可观察缺陷，全部让"报告成功"与"真实产物/真实空间内容"脱钩：

1. **D-T05-CHAIN1（chunked-run 假绿）**：`.published` marker 只验证身份键，不验证声明产物现状。产物被删除/替换后，任何重复运行（任意目录/工作区，经共享 `/tmp/sicnu_operators` 状态）都零工作返回成功——`[warning] Skipping asset registration; output not openable` 后 `Pipeline succeeded`，无文件（r28-t05 root-caused；`resumable_tile_run.cpp:301-305` 直接 alreadyPublished）。
2. **虚拟栅格仅换 SRS 标签**：输入彼此同 CRS 但 recipe 显式 `targetCrs` 不同的场景不触发重投影判定（`virtual_raster_preflight.cpp` 只比较输入之间 CRS）；provider 把 targetCrs 写进 `<SRS>` 却仍按输入 CRS 网格计算 SrcRect/DstRect（`virtual_raster_source_provider.cpp`）——产出的"重投影"产品空间上完全错误。旋转/剪切输入同样无法由轴对齐矩形表达。跨单位无显式分辨率时更会四舍五入出 1×1 退化产品"成功"创建。
3. **H1 制图任务全透明导出**：`cartography:produce/export/compose` 的 `requireQgisHost()` 只查 QgsApplication 存在，不查线程；TaskCenter 的 detached worker 线程上 QgsLayoutExporter 渲染出全透明 PNG（mean alpha 0.0，99.8% 像素 AE）——每个经 dock/workflow 提交的制图任务都交付空白"成功"（r28-t04 HANDOFF）。

## 已有实现基线

R28 已交付 cancellation、import session 分步驱动、有界 slice、OutputCommitter、#1826 完整批次 verdict、change-streaming 的 per-operator 缓存重开校验（本轨保留其 fail-closed 语义并收敛为公共契约）。spectral index 的 canonical params 在本 base 已含 outputPath（历史 handoff 描述过时）——剩余缺陷是 marker 不验证产物现状与输入内容漂移。

## 本次功能增量

- **WP02（B1/B2）**：`ResumableTileRun` R8——可选 `publishedOutputValid` 回调；marker 命中先验证，不可证明则仅删 marker 降级重建（journal/tiles 按 R2 复用，不重算）；remove 失败 fail-closed。`ChunkedRunOptions::verifyPublishedOutput` + commitSpec 默认 stablePath 探针（存在+非空+非目录）。`rs:spectral_index` 家族：发布时写入 `SICNU_SPECTRAL_IDENTITY`（canonical 参数 SHA1），marker 命中时重开产物验证身份+几何；输入以 (size, mtime, ctime) 进入运行身份（postfire 同）——同路径内容替换强制重算。
- **WP05/WP06（B3）**：preflight 新增 target-differs 判定（canCreate 需显式分辨率，跨单位安全）；provider 经 `geo::warpRaster`（staging+验证+原子发布）把 CRS 不同/旋转/剪切输入 warp 到对齐的目标网格，VRT 以 pinned grid 组合；recipe-digest 版本化中间文件 + 陈旧代清扫；`<1` 像素网格拒绝。`createVirtualRaster` 打开 RequiresReprojection 门；relocate 经同一布局入口（不再降级回 relabel）。`isSameCrs` 接受 authid 字符串（修 EPSG-string vs WKT 永假）。`-te/-tr` %.17g 往返格式化。
- **WP09（B5）**：`RSOperator::requiresHostQtThread()` 执行线程契约（默认 false，加法）；cartography 三个渲染 operator 标记 + `requireQgisHost()` 线程检查（typed 拒绝，缺陷不可静默回归）；JobEngine dispatch 期 queued invocation + worker 信号量等待（非 BlockingQueuedConnection），cancel 5s 优雅期后 abandoned 标记 + Cancelled（迟到投递 no-op，无悬垂）；`waitForTask/waitForPipeline` 在 app 线程按切片 processEvents（host lane 可达）。
- **WP01/WP11（B6）**：六假设 CLI 基线脚本 + 三条完整链（链① CLI NDVI 删除产物自愈重跑；链② 混合导入取消→重启恢复→虚拟目标网格→recipe 重开；链③ TaskCenter 制图真实 ink + 失败不污染既有产物 + 再成功）。

## 与其他轨边界

T01 算法/T03 harness/T04 UI 未触碰；`cartography_operators.cpp` 仅执行线程能力声明与 host 检查（T04 只写 UI）。接口全部加法（见 HANDOFF.md）；`workflow_run_coordinator`/`output_verifier` 本轮无需改动（缺陷根因不在彼处）。

## 接口变化

见 `.planning/r29-t02-data-runtime/HANDOFF.md`（4 项，全部向后兼容加法）。

## 真实验证（绑定 HEAD，构建 dev-default Debug -j4 双锁）

- 新增：`test_r29_t02_published_claim`（73 断言 ✅，含 R8 demote 后 R2 复用 4/4）、`test_r29_t02_virtual_warp`（144 断言 ✅，独立 OGR 像元/extent oracle）、`test_r29_t02_chains`（100 断言 ✅）。
- `wp01_published_claim_baseline.sh`：16/16 ✅（H-C 自愈、H-D 内容漂移、H-E 跨工作区）。
- `SICNU_CP11_THREAD_PARITY=1` cp11 交付契约 case：24 断言 ✅（三表面字节一致 + 真实 ink）。
- 回归：68 项匹配批量（chunk 家族/output_committer/io_atomic_failures/workflow_cache_e2e/r28_t02_*/virtual_raster_*/swarm_fix_data/task_center/job_engine/execution_plane）——结果见 LEDGER/ACCEPTANCE。
- 独立 review：R1（新上下文 reviewer）1×P0+3×P1+7×P2/P3 全部处置（REVIEW_R1.md）；R2 复核绑定最终 HEAD（REVIEW_R2.md）。

## 必要限制

- 同路径 byte-identical 重写 fork 新 chunk 状态（保守指纹代价，HANDOFF 记录）。
- warp 中间文件进程中途死亡可能残留（可再生成，无害）。
- host lane 无 cancel 时等待无界（前提：宿主服务事件循环；#1767 timeoutMs 兜底）。
- 未执行环境通道：X11 GUI 实机渲染（仅 offscreen 验证）、Windows（portable 路径已按 ACP-safe 规约处理但无实机）。

Refs #1825（change-streaming 缓存校验保留）、Refs r28-t05 D-T05-CHAIN1（无对应 issue，root-cause 记录于其 ISSUE_TRIAGE）、Refs r28-t04 H1（同上）。
