# R23-B PERF_BASELINE — 本机可复现性能记录

环境：cachyos x86_64（62GB），Release 构建，系统 Qt6.12/GDAL 3.13.3/OpenCV 5.0；测试均为串行（ctest -j1 / 单二进制直跑），QT_QPA_PLATFORM=offscreen。

## 直接测量（本轮变更引入/影响的路径）

| 项 | 数值 | 说明 |
|---|---|---|
| W1 stub-compile gate 全程 | **3.8s 墙钟（3.71s user）**；ctest 记录 3.47s | 两 TU `-fsyntax-only`；每 PR 固定成本，可接受 |
| test_r23_b_derived_output_roles 全套 | ~0.7s（3 用例 ×7 acquire+preflight，OpenCV DNN 读 fixture） | W2 门为纯 preflight 分支，零计算开销 |
| test_r23_b_run_metric_transaction 全套 | <1s（含 fork 崩溃用例） | W3 单事务 vs 两调用：同为单连接 SQLite，BEGIN IMMEDIATE 数不变（1 次 vs 2 次），净省一次 commit fsync 路径 |
| test_model_runtime 全族 | **1.47s 墙钟（110,440 断言）** | 与 r21-B 记录口径一致（无回归） |

## 对照声明（O3）
- W2 前置门在引擎热路径外（run() 预检段，每 run 一次 O(1) 字符串比较）；全族墙钟与基线一致。
- W3 导入路径：少一次事务提交（原 saveMetricRecord+upsertRun 两次 BEGIN/COMMIT → 一次），崩溃窗口收敛为 0。
- W4 probeRemote：CPLHTTPFetch → httpFetchStatus 受控传输。首跳行为等价；重定向场景从"未检查跳转"变为"每跳检查+DNS pinning"——安全属性变化，非吞吐目标。stub-server 实测 probe 契约族总时长与改造前同量级（25 断言/6 用例 <1s 族内）。
- 本轮无 ≥15% 吞吐目标声明：所选 slice 均为正确性/契约修复；未发现需声明的热点回归（模型运行时全族 1.47s 与基线持平）。诚实记录：未做大栅格/远程缓存吞吐基准（无新热点证据支撑其优先级，未虚报）。

## 资源与约束遵守
- 全程 flock 全局锁 + `--parallel 2`；构建峰值 RSS 未越 62GB 主机安全线（并发三轨串行）；ctest 串行。
- 磁盘：build-r23-B ~35GB（含 847MB qgis_core.so 调试符号）。
