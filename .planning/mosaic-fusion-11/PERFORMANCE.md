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
| build 期资源 | 每 60s `ps`/`uptime` 采样（Phase 1 起记录） | 待记录 |
| 逻辑规模内存不变性 | test_mosaic_scale：合成 tile source 计数峰值工作字节 @ 1k/100k 逻辑 tiles | 待记录 |
| env 门控实图 E2E | `EXP_MOSAIC_SCALE_E2E=1` ctest -R test_mosaic_scale（opt-in） | 待记录（默认不跑） |

## 主机资源监控记录（build 期 60s 采样）

（追加）
