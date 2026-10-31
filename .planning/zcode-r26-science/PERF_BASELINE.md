# PERF_BASELINE — R26-science（2026-10-31, 本轨 HEAD）

方法：注册表解析 rs:change_detection（registry.create）→ 流式差值+手动阈值掩膜
→ GeoTIFF 产物；Debug 构建；每轮独立进程 ×5；/usr/bin getrusage 峰值 RSS。
运行器：/tmp/perf_r26.cpp（ad-hoc，未入库；方法论与 E2E 测试同构）。
宿主：16 核 / 62GB（共享），QT_QPA_PLATFORM=offscreen，-j4/flock 构建纪律下。

| 规格 | 中位耗时 | 区间 | 峰值 RSS | changedPixels | changedArea(m2) ×5 一致 |
|------|---------|------|----------|---------------|------------------------|
| 256² | 32.8 ms | 31.7–33.8 | 171 MB | 256 | 230400.0 逐位一致 |
| 1024² | 134.4 ms | 133.5–135.8 | 183 MB | 1024 | 921600.0 逐位一致 |
| 2048² | 462.8 ms | 452.8–474.1 | 222 MB | 2048 | 1843200.0 逐位一致 |

吞吐：1024²≈7.8 Mpx/s；2048²≈9.1 Mpx/s（含 I/O 与掩膜形态学）。

- 本轨无性能优化主张（correctness-only PR），故无 master 对照提速声明；
  上述为 HEAD 基线，供后续优化轨对照（NOT_RUN: master 对照、Release 构建）。
- 内存峰值受 Qt6/GDAL 运行时 (~170MB 基线) 主导；2048² 增量 ~52MB 与
  分块流式设计一致（无整幅 float32×2 副本膨胀：4.19M px 全幅缓存应为
  ~33MB/层，观测增量与之同量级）。

## 附带发现（CROSS_TRACK，未修，非本轨写区）
同一进程内经 RSOperatorRegistry 第二次运行 rs:change_detection（makeMask）
抛 "Failed to reopen magnitude raster for masking"：run2 的 magnitude 临时
文件从未创建。直连构造 RsChangeDetectionOperator 的 41 个既有用例同进程
反复运行无此问题 ⇒ 根因在 registry 运行时包装层（rs_operators_init/
chunked_run 的 runtime workspace / commitSpec 状态残留），不在本轨 OWNERSHIP。
复现：/tmp/perf_r26.cpp（每轮 fresh registry.create 仍复现，run1 恒成功）。

## S2 修复后的 capped 路径内存画像（R1 P1-1 处置）
- cap 后置要求候选值全部参与 NoData 过滤，故 capped 分支的 out.X 按
  **ROI 候选总数**（而非历史 cap×类数）预分配；已去除 R1 指出的全局
  order/sorted* 三次全量拷贝（改为类内 (pixel,rank) 配对排序）。
- 残余画像：候选 = N_roi 像素时 out.X 峰值 = N_roi × B × 4 字节
  （100 Mpx × 3 波段 ≈ 1.2 GB）。无 NoData 场景与修复前逐位一致
  （test_training_data_extraction 钉值测试仍通过）；含 NoData 场景
  为正确性优先的资源取舍（cap 对"保留样本"契约优先于内存上限）。
- NOT_RUN: 彻底方案（NoData 判断下沉 scanline 读循环、X 只积累有效样本）
  属重构级改动，超出本 correctness 轨范围，建议后续 perf 轨接手。
