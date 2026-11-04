# PERF BASELINE — R26-02 workbench

配置：同一 Debug 构建（build-r26-workbench，CMAKE_BUILD_TYPE=Debug，ENABLE_TESTS=ON），QT_QPA_PLATFORM=offscreen，
单进程串行，宿主 -j4 构建锁下执行，5 次取中位数。

## 新测试 wall-clock（HEAD 修复后）
| 目标 | 中位数 | 5 次明细 |
|---|---|---|
| test_r26_workbench_spectral_ux | 0.32 s | 0.32/0.32/0.32/0.32/0.32 |
| test_r26_workbench_roi_histogram | 4.46 s | 4.48/4.49/4.43/4.46/4.44 |
| test_r26_workbench_dialogs | 0.67 s | 0.66/0.67/0.67/0.67/0.66 |
（其余三个目标含模态拦截/分类窗口构造，wall-clock 主要由 Qt 事件循环构成，不作性能门禁。）

## 生产路径开销判定
- 谱线 NoData 检查：每波段一次 `GDALGetRasterNoDataValue`（元数据读）+ 至多两次 double 比较；单像素 × bandCount 量级，
  相对 `GDALRasterIO` 本身为噪声级。与 exp_gui 旧版及 roi_statistics 既有做法同阶。
- 直方图 `bandDataReady`：仅在扫描落地且至少一个 band valid 时 emit 一次；HistogramStretchWidget 侧若用户未手动
  调整则调用既有 `updateMinMaxFromLayer()`（一次 bandStatistics/缓存读），不新增后台工作。
- ROI 统计按钮/摘要复位：O(1)。
- 合并类重标注：单次 feature 遍历 + 每源类要素一次 `changeAttributeValues` + 一次既有全量 rebuild
  （该 rebuild 在合并前也会因任何样本编辑触发，属既有成本，非新增常态开销）。
- busy 守卫/关窗确认：仅在用户主动操作路径。
未引入轮询、定时器或后台循环；无共享宿主墙钟硬阈值门禁（按任务约束）。

## 峰值 RSS
未能采集（容器无 /usr/bin/time 与 GNU time；本轨未使用 sanitizer 构建）。标注 NOT_EXECUTED。
