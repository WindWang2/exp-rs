# PERFORMANCE — 资源模型与规模证据

## 资源模型（设计上限，静态）

- rs:quality_mosaic 流式窗口 512²：每 tile 常数个工作缓冲（输出 tile、每输入 tile、cost/label 窗口、
  blend 权重窗），峰值内存 = O(inputs × tile²) + O(seam band) 而非 O(全图像素)。
- seamline DP：O(重叠带宽×带高) 局部、每对场景一次；不持有全图 label（随窗口流式消费 label 光栅，
  label 光栅本身按窗口写入临时/内存带）。
- balancing：每对 overlap 采样统计 O(overlap pixels) 时间、O(1) 额外内存（两遍流式统计）。
- provenance：UInt32 全图波段走 GDAL 窗口写，不驻留内存。

## 实测证据（追加式）

| 证据 | 命令/方法 | 结果 |
|---|---|---|
| build 期资源 | 每 60s `ps`/`uptime` 采样（全量 qgis_core+processing 构建） | 峰值编译器 RSS ≈ 5.4 GB（远低于 64 GB；保持 `-j2` 无需降档；负载 15-17 含并行 track，< 24 阈值） |
| 逻辑规模内存不变性 | test_mosaic_scale：CountingSampler 峰值缓冲字节 ≤ 8×窗口（2048² 场景 64² 窗口）；BinnedSeamCost cells ≤ 512² @ 100k 逻辑 tile | PASS（4/4 用例） |
| env 门控实图 E2E | `EXP_MOSAIC_SCALE_E2E=1 ./test_mosaic_scale "Scale: opt-in real-raster E2E stays green when enabled"` | **exit 0，All tests passed**（1600² 双场景 @ 40² 窗口，峰值缓冲 ≤ 8×窗口） |
| 融合基准回归 | `./test_perf_fusion`（2048² GS/Brovey/IHS/HPF 基准 + 正确性断言） | **exit 0，12135 assertions passed**（GS 2048² ≈ 1.38 MPix/s；wall-clock 仅记录，不作 correctness gate） |

## 主机资源监控记录（build 期 60s 采样，节选自 .planning/build_monitor.log）

```
00:31:20 load=17.83 16.25 14.62 rss_mb=4551
00:38:20 load=16.88 17.31 15.78 rss_mb=5450
00:39:20 load=16.75 17.20 15.84 rss_mb=5495
```

（注：负载含主机上另一并行 track 的构建；本 track 严格 `-j2`，无需降档 `-j1`。）
