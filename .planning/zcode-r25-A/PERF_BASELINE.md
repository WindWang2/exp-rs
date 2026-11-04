# R25-A PERF_BASELINE（report-only 车道，无墙钟门）

环境：Linux 共享宿主，Debug build，`-j2` 构造/测试串行，QT_QPA_PLATFORM=offscreen。
车道：`test_r25_a_perf_baseline`（ctest 或二进制直跑；每次测量 run 同时是正确性 GREEN 断言）。

| 基线 | 数据 | 中位吞吐 | 5 次 | VmHWM 峰值 RSS |
|---|---|---|---|---|
| rs:terrain_flow flow_accumulation（含 DEF-2 uint64 精确路径） | 2048×2048 倾斜面 DEM | 0.56 Mpx/s | 0.553/0.559/0.562/0.562/0.562 | 364.8 MiB |
| rs:spectral_resample 50→3 bands | 1024×1024×50 | 1.26 Mpx/s | 1.210/1.219/1.256/1.266/1.298 | 597.0 MiB |
| rs:post_classification_change | 1024×1024，4 类 | 9.07 Mpx/s | 8.536/9.031/9.069/9.193/9.195 | 597.4 MiB |

说明：
- flow_accumulation 吞吐含 priority-flood 填充 + D8 路由 + uint64 计数；峰值 RSS 360 MiB 低于 2^28 格预算下文档化的 ~8 帧×64MiB 界（2048²=4M 格）。
- 无 master 对照计时（共享宿主 -j2、跨轨并发波动 >5% 属噪声，按 DECISIONS D6 不做硬门）；后续修订可用同一车道对比。
- resample/change 的 RSS 含 catch2 + GDAL 进程基线（~180 MiB 起跳于 terrain 之后共享）。
