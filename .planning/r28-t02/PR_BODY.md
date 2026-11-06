[R28-T02] 数据与执行运行时：可取消、可恢复、不污染产物

## 基线与集成

- base = head = 50cc3255c5（origin/master@2026-11-04T18:04Z 调查时刻；提交前复核 master 未移动，无需 rebase）
- 分支 agent/r28-t02-data-runtime-20261104T180429Z，worktree 独占，6 commits（feat + test + docs×2 + 幂等修复 + R1 闭环）
- check_merge_registration_integrity：1149 目标（+5），丢失 0，冲突标记 0，重复 0

## 用户问题（本轨解决）

1. **批量导入一旦开始就停不下来**：UI 的取消按钮是 queued invocation，而 ImportSession.proceed() 同步占线程到整批结束。→ 分步协作调度（checkSteps/proceedSteps，QTimer(0) 链驱动），取消在 ≤1 项内落地，无 processEvents 重入。
2. **恢复的真相只有 journal**：journal 说的和 catalog/磁盘不一致时静默跳过。→ journal 记录 check 时 identity token；verifyFacts() 复核文件存在性+内容身份，替换/移动可见告警；恢复以 catalog 为唯一注册权威（closed_loop 实证 5/5 findByPath）。
3. **远程 COG 的后端裸奔面**：canonical fabric 读取链在 range cache 未装时把裸 http URL 直接交给 GDAL（正是 #1765 要堵的洞的域内残余）。→ typed GeoError(InvalidArgument) fail-closed；local/object-store 分支不变。
4. **取消失败簇（#1812 遗留 77 失败的一部分）**：本地复现 3/60（TC_CNC_01/03/05）→ 根因=fixture 违反 RIO-10 取消协议（mock 观察 flag 后正常 return，引擎按契约记 Succeeded——迟到取消永不改写成功）→ fixture 改为 throwIfCancelled 展开 → 28/28。分类诚实记录：非 state-machine bug。
5. **abandoned worker 不可见**：有界 shutdown 放弃 stuck worker 只有返回值。→ 叠加 `shutdown_abandoned_workers` trace 事件（stuck=N）。

## 用户可见增量

- `data preflight <paths...>` CLI：批次导入体检 JSON（sicnu.import.preflight.batch/1：逐源 verdict+counts+suggested_action=import-ready/resolve-choices/fix-or-drop-refused）
- 导入恢复状态/拒绝原因/替换-移动告警（summary.journalError/journalDurable/item.message，schema 加法兼容）
- 取消/失败可解释（typed cancelReason、watchdog 归因、stuck 计数）
- 真实推理资源与设备摘要（payload.backend/device/tiles + plan.batchWorkingSetMiB）

## Issues

**Fixes（完整解决且当前复核）**：#1743 #1764 #1765（后端侧；GUI 侧 HANDOFF→T04）#1766 #1767 #1803 #1705 #1558 #1575 #1576 #1577 #1579 #1583 #1584 #1585 #1603 #1604 #1605（master 实码核验为已修，本 PR 复跑全部对应回归后以 Fixes 关闭；#1586 复核见 triage 表）

**Refs**：#1699（墙钟断言治理跨轨）、#1812（取消簇已消，其余 74 失败归各域）

## 共享接口/冲突提示

- tests/CMakeLists.txt：仅追加 3 个 r28-t02 注册块（与 #1813/#1814/#1815 无 target 冲突，文本相邻）
- .goal-loop-ledger.md：prepend
- .gitignore：追加 r28-t02 planning 白名单（markdown only，惯例同 r27-t02/t05）
- HANDOFF.md：T04（stac_browser_dialog 接 safeRemoteRasterPath + 导入面板消费分步 API）、T03/T05 接口说明

## 测试命令与真实数量

构建：`cmake --build build-r28-t02 -j2`（ccache + raise-compiler-stack；OpenCV 5 prefix）
测试（QT_QPA_PLATFORM=offscreen，串行，本机全部实跑）：
- 本轨新增 13 case（4 套件）+ 1 场景 B 套件：import_session_runtime 7（50 断言）、geospatial_interop 4（27）、infer_cpu_loop 1（452，真实 cv::dnn）、closed_loop 1（43）、task_lifecycle_batch 1（20）
- agent_ops_task_center_executor 28（101 断言；master 25/28 → 28/28）
- WP01 基线 60 例（job_engine/task_center(_12)/execution_plane_7-9/concurrency_stress/workflow_cancel/r27_t02 全套）：57 绿 + 3 失败（即上述簇，已修）
- WP08/09 回归批 16 套件全绿：model_catalog_v2 24 例/436 断言、model_runtime×3+stress、tile_semantic_equivalence、ensemble_scene 9/504、ensemble_detection、leakage×3、offline_mode 10/34、r27_t02_safe_remote、io_atomic_failures 14、r25_b_inference_plan 6
- **两遍验证**：全部第二遍独立重跑通过（真实计数见 ACCEPTANCE.md）

## NOT_EXECUTED（据实标注）

Windows lane（仅编译目标存在）；GPU/TensorRT/OpenVINO/ONNX Runtime 真实运行（宿主无硬件/库；ensemble member gate 与 provider 契约由既有套件覆盖）；test_model_runtime_bench（性能 lane 非本轨验收项）

## 性能

未宣称任何百分比提升（无对照基线）。资源结论为有界性核验：bounded logLines（#1705）、租约有界（#1764）、shutdown deadline+stuck 计数（#1767）、throttle deadline（#1603）、事件环固定容量（#1605）——区分真实 bounded 工作集 vs 线性增长面的证据见 BENCH 口径（.planning/r28-t02/ACCEPTANCE.md G4）。

## R1/R2

- R1（独立对抗评审 agent，全新上下文，含实跑复现）：**SHIP**（0 P0 / 0 P1 / 2 P2 / 8 P3）。评审者独立重建基线 fixture 复现了取消簇、直跑 13/13 新套件与 28/28 agent_ops、实调 CLI。全部 findings 已在 commit 8a5d8f3c14 处置并复验绿（P2-1：cube/mirror 补 CacheGuard+--allow-private-network+usage；P2-2：HANDOFF 集成注意；7×P3 全修）。报告：.planning/r28-t02/REVIEW_R1.md
- R2（第二轮独立 reviewer，另一独立上下文）：**VERDICT: CLEAN / SHIP**。逐点验证 R1 全部修复（含实跑：53/7、43/1、21/1、27/4、452/1、101/28、97/8 与声明逐计数一致）；新发现 1×P3（mirror 旗标顺序，库契约不一致、真实二进制不受影响——已修）+2×P4（文档指针已修；环境项移交）。报告：.planning/r28-t02/REVIEW_R2.md

## 已知风险与回滚

- fabricCachedPath 的 typed 拒绝会照亮"cache 未装就开远程"的调用方（此前静默裸读）——CLI 已装 cache（CacheGuard）；若未来调用方触发，错误信息含修复指引。回滚=还原 object_store.cpp 单文件。
- ImportSession 新 API 纯加法，旧调用面（CLI monolithic）语义不变。
- 回滚整体= revert 6 commits（无 schema 迁移、无数据格式变更；fabricCachedPath 单文件可独立回滚）。
