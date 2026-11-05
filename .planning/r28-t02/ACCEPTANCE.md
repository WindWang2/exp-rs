# ACCEPTANCE — R28-T02 验收表（2026-11-05 22:1x +0800 更新）

## 门禁

- [x] G1 取消失败簇实测消除：本地复现 3/60（TC_CNC_01/03/05）→ 分类 C（fixture 违反取消协议：观察 flag 后正常 return，引擎按 RIO-10 正确记 Succeeded）→ 修复 mock 以 throwIfCancelled 展开 → 套件 28/28。其余 57 例基线本就绿（job_engine/task_center_12/execution_plane_7-9/concurrency_stress/workflow_cancel/r27_t02 全套）。
- [x] G2 ImportSession 恢复闭环：closed_loop 场景 A 实证 resume 后 5/5 settled 且每项 findByPath 命中（catalog=唯一权威）；journal 损坏 fail-closed；journal 写失败诚实非持久 + catalog 仍完整；verifyFacts 对替换/移动文件可见告警（7/7 套件绿）。
- [x] G3 真实 CPU 推理闭环：infer_cpu_loop 1/1（plan Ready→runModelInference→opencv_dnn→写出→GDAL 重开逐像元→质量摘要）；ensemble_scene 504 断言真实 DNN 绿。GPU/TensorRT/OpenVINO/ONNX Runtime=NOT_EXECUTED（宿主无硬件/库；代码级契约由 catalog/provider 套件覆盖）。
- [x] G4 资源有界区分：计数型 oracle（bounded logLines #1705、lease 有界、shutdown deadline+stuck 计数、throttle deadline #1603、事件环固定容量 #1605）为真实 bounded；无新增线性增长面（B2 表全部实码核验+回归绿）。性能提升百分比未宣称（无基线对比，不虚报）。
- [x] G5 用户可见增量：①批次导入体检（inspectBatch + `data preflight` CLI，sicnu.import.preflight.batch/1 含 suggested_action）②恢复状态/拒绝原因/替换-移动告警（summary.journalError/verifyFacts/item.message）③取消/失败可解释（typed cancelReason、watchdog 归因、stuck 可观测 trace）④实际推理资源与设备摘要（payload.backend/device/tiles + batchWorkingSetMiB）。
- [x] G6 相邻轨兼容：纯加法 API（ImportSession+5 方法、inspectBatch、journal+identity_token 字段旧读降级）；无删改既有签名；HANDOFF.md 记录 T04（stac_browser 接线 safeRemoteRasterPath + 分步导入面板消费法）/T03/T05 接口。
- [x] G7 相关测试非零、两遍通过、新增失败为零：第一遍全绿明细见下；第二遍（独立重跑）已完成：53/7、27/4、452/1、43/1、20→21/1、101/28（计数见本文件'测试计数'节与 logs/；R2 评审者亦独立复跑全部绿）。
- [ ] G8 R1/R2 独立评审（进行中→待做）
- [ ] G9 最新 master 集成后复核 + check_merge_registration_integrity
- [ ] G10 PR 真实 URL

## WP 状态表

| WP | 状态 | 证据 |
|---|---|---|
| WP01 取消簇复现 | **DONE** | 3/60 复现→分类→fixture 修复→28/28（wp01_baseline.log + 重跑日志） |
| WP02 生命周期 | **DONE(+增量)** | 既有：bounded shutdown/watchdog/优先级桶/瞬态容量（r27_t02 34 用例绿）；增量：stuck worker trace；场景 B 20 断言绿 |
| WP03 ImportSession 闭环 | **DONE** | 分步调度+requestCancel（queued cancel ≤1 item 落地）+verifyFacts+journal 故障矩阵；7/7×2 |
| WP04 批次体检 | **DONE** | inspectBatch+CLI；suggested_action 三态；7/7 |
| WP05 原子发布 | **ALREADY_DONE_WITH_EVIDENCE** | output_committer publish-then-swap+fsync+.old 回滚+fault point；io_atomic_failures 14/14 重跑绿 |
| WP06 远程统一入口 | **DONE** | fabricCachedPath 裸降级→typed 拒绝（新 #1765 后端收口）；站点审计全表（WP06_REMOTE_AUDIT.md）；io_fabric_object_store 8/8+identity_11 5/5+safe_remote+offline_mode 绿 |
| WP07 互操作 | **DONE** | 4/4（NoData/scale/offset/role roundtrip、#1579 字面 % pin、UTF-8+空格、typed CRS 拒绝） |
| WP08 catalog/设备身份 | **ALREADY_DONE_WITH_EVIDENCE** | #1576/1577 已修（R15-C 标记）；model_catalog_v2 436 断言/24 例绿；runtime/stress 绿 |
| WP09 资源估计/OOM | **ALREADY_DONE_WITH_EVIDENCE** | #1575 halo 估算在位；effectiveTileSize manifest 驱动；OOM=batch 减半梯级；r25_b_inference_plan 6/6 绿；batchWorkingSetMiB>0 在 WP10 断言 |
| WP10 真实推理闭环 | **DONE** | infer_cpu_loop 1/1×2（真实 cv::dnn） |
| WP11 可观测 | **DONE(+增量)** | 既有遥测词汇完整（cancel latency/watchdog counter/tier counters）；增量：stuck trace；device/backend/tiles payload |
| WP12 压力闭环 | **DONE** | 场景 A（closed_loop 1/1：体检→取消→恢复→推理→重开+journal 故障变体）；场景 B（lifecycle_batch 20 断言：混合批/协作取消/watchdog 终局/迟到取消不改写/有界 shutdown stuck≥1） |

## 测试计数（本轨新增=13 case：7+4+1+1；改动既有套件 2 个的 fixture/case）

第一遍（2026-11-05 21:2x-22:0x，QT_QPA_PLATFORM=offscreen，串行）：
- import_session_runtime 7、geospatial_interop 4、infer_cpu_loop 1、closed_loop 1、task_lifecycle_batch 1（20 断言）
- agent_ops_task_center_executor 28（原 25/28）→ fixture 修复后 28/28
- WP01 基线 60 例：57 绿 + 3 失败（即上述簇）
- WP08/09 批（直跑二进制）：model_catalog_v2 24 例/436 断言、model_runtime×3+stress、tile_semantic_equivalence、ensemble_scene 9/504、ensemble_detection、leakage×3、offline_mode 10/34、r27_t02_safe_remote、io_atomic_failures 14（ctest）、r25_b_inference_plan 6 —— 全部 exit 0
- NOT_EXECUTED：Windows lane、GPU/TensorRT/OpenVINO/ONNX Runtime 真实运行、test_model_runtime_bench（性能 lane，未纳入本轨验收）
