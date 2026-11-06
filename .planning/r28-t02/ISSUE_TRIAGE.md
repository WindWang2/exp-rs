# ISSUE_TRIAGE — R28-T02（基于 2026-11-04T18:04Z open issues 快照 + master=50cc3255c5 实码核验）

## A. FIXED_OPEN（master 代码已含修复，本轨复核后用 Fixes 清偿）

| Issue | 严重度 | 内容 | master 实码证据 | 本轨动作 |
|---|---|---|---|---|
| #1743 RIO-1 | P1 | addJoin({A,A}) 绕过单消费者 | chunk_graph.cpp:86-97 同调用重复 typed 拒绝 | 复跑 #1810 回归（8 用例）→ Fixes |
| #1764 RIO-2 | P2 | worker 租约表无界 | local_worker_pool.cpp:165,256,284,334 m_leases.reset 全路径 | 复跑租约回归 → Fixes |
| #1765 RIO-4 | P2 | STAC COG 裸 /vsicurl/ 绕过 SSRF | **PARTIALLY_FIXED**：后端权威 src/geospatial/remote/safe_remote_open.* 已在位；src/app/dialogs/stac_browser_dialog.cpp:244-265 仍 GDALOpenEx(vsicurl) | 后端侧复核（WP06）→ Refs + HANDOFF.md 给 T04（GUI 换用统一入口） |
| #1766 RIO-5 | P2 | ResumableTileRun 无独占锁 | resumable_tile_run.h:35-47 exclusive_path_lock，不 unlink | 复跑 → Fixes |
| #1767 RIO-8 | P2 | shutdown 无界 join | job_engine.cpp:52-190 immortal singleton + waitWorkersExitedBounded + stuck 计数 | 复跑 + WP02 深化 → Fixes |
| #1803 RIO-P3 集合 | P3 | RIO-3/6/7/9/10/11/12/13/14 | #1810 body 声称全修（RIO-12 声明残余 ~13 处跨轨站点） | 逐子项复核：子项修复确认 → Fixes；RIO-12 残余如实标注跨轨 HANDOFF |
| #1705 | P2 | JobEngine logLines 无界 | job_engine.cpp:875-905 有界逐出 + droppedLogLines + task-center 绝对索引换算（fb1b2e8489） | 复跑 appendLog/snapshot 回归 → Fixes |

## B. CONFIRMED_OPEN（本轨范围内需要真实修复）

| Issue | 严重度 | 内容 | 计划 |
|---|---|---|---|
| #1699 | P2 | fast/ASan 层大量墙钟阈值断言 | WP01 涉及的取消簇测试如用过紧 wall-clock 一并治理；全仓治理跨轨（部分 HANDOFF） |

## B2. FIXED_OPEN 候选（2026-11-05 实码核验——已由中间轮次修复，本轨复跑回归后 Fixes 清偿）

| Issue | 严重度 | 内容 | master 实码证据 |
|---|---|---|---|
| #1558 | P1 | 两个 issue 回归用例断言恒真 | test_e2e_open_issues.cpp:817-844（#799 waitForTask+终态断言）、:892+（#784 完整 fixture）已被 #1812 重写 |
| #1583 | P2 | auditFolds foldCount 当基数 | fold_audit.cpp:78-95 从 assignments 推导基数（含解释注释） |
| #1584 | P2 | overlapping_patch 只查 left | leakage_audit.cpp:652-653 left+right 双侧门槛 |
| #1585 | P2 | pre_post 单向引用静默丢弃 | leakage_audit.cpp:466-476 仅对称对去重、单向仍上报 |
| #1586 | P2 | saveLeakageReport SELECT 与写入不一致 | dataset_store_splits.cpp:232+ 带 R15-B(#1449) 生命周期守卫（细目以回归跑为准） |
| #1587 | P2 | mirrorAllAssets 丢弃写入失败 | 待回归跑核验（grep 未命中原站点；实施位置在 workspace_service） |
| #1603 | P2 | throttleWait 无上限 | execution_governor.cpp:547-635 deadline+maxPolls+cancel 谓词+ChunkCancelled+telemetry |
| #1604 | P2 | FileTraceSink 每条重建 writer | trace.cpp:261-269 thread_local 流一次打开复用 |
| #1605 | P2 | 事件环逐条 erase | execution_telemetry.cpp:64 固定容量环+写游标 |
| #1575 | P2 | effectiveBatchSize 忽略 halo | tile_inference_engine.cpp:4325-4377 halo 窗口+to_input 取大者（#1575 注释在位） |
| #1576 | P2 | OpenVINO ProviderTraits{} 仅 cuda:0 | openvino_provider.cpp:314+ 注释+selectDevice typed 拒绝 |
| #1577 | P2 | TensorRT/OpenVINO artifactPath 硬编码 | 两处 mArtifact=真实加载路径（R15-C C05/B8） |
| #1579 | P2 | resolveAgainst 字面 % 误拒 | resource_uri.cpp:973-1004（R15-B #1505 T9）；WP07 补字面-% 正例 pin |

## C. 其他轨（不在本轨动代码）

- #1549（preflight allowAssumed 共享缓存空操作）→ T01/T05
- #1751 BCT-22（505 处 sleep）→ 全仓测试卫生，本轨只治理自己触碰的用例
- #1732/#1733 等 P0 → 其他轨已处理或跨轨
- F 系列（#1788-#1795）、WE 系列、OS 系列、AS 系列、SPP 系列 → T04/T05/T01/T03
- #1560（src/app runGdalTask 取消钩子空 lambda）→ src/app 归 T04；但**取消语义底座**若需本轨提供可接线原语，本轨提供并记录于 HANDOFF

## D. NOT_REPRODUCED / 待实跑分类

- #1812 记录的 TaskCenter/ExecutionPlane cancellation cluster（无 issue 编号，PR 记录）：WP01 首个任务=在本地 master 基线实跑复现并分类
