# R24-B PERF_BASELINE（O-B4；Windows 本机，5 次实测）

**声明：本轨不做任何加速宣称**。全部修复为正确性/竞态/覆盖类；下列数字用于证明修复后的热路径成本无病理性恶化，并为后续性能轨提供基线。

- 口径：每套件完整进程 5 次（`Start-Process` 独立运行，offscreen，同一宿主、无并发构建），wall= Stopwatch，CPU=TotalProcessorTime；数据 2026-10-27，RelWithDebInfo。

| 套件（内容） | wall_avg | wall_min | wall_max | cpu_avg | peakRSS |
|---|---|---|---|---|---|
| dataset_txn_gate（80 轮双连接 barrier racer：draft/stage/commit/addSamples/addAssets + 40 个 200 资产清单版本全生命周期） | 2068.1ms | 2033.7ms | 2177.4ms | 656.2ms | NOT_CAPTURED |
| lineage_reattach（4 次完整 bundle 导出→完整性门→导入→lineage 重挂→图遍历） | 2043.5ms | 2037.7ms | 2046.9ms | 543.8ms | NOT_CAPTURED |
| leakage_bucket（极端坐标审计 + 损坏库 open 拒绝） | 1039.9ms | 1037.0ms | 1043.6ms | 84.4ms | NOT_CAPTURED |
| metamorphic（7 次完整 tile 推理 96×64×3 + 尾批回归 97×32） | 1037.3ms | 1034.5ms | 1040.9ms | 396.9ms | NOT_CAPTURED |

- 方差：各套件 wall 极差 ≤ 7%（<2.1ms/1.04s 量级），宿主噪声可控。
- **base(5fe642ec6) 对照：NOT_EXECUTED**——需要第二座完整构建树方可对同负载双端计时；本轨不宣称加速，故不构成验收缺口（O-B4 禁止"无证据声称加速"，本轨零加速宣称）。
- peakRSS：NOT_CAPTURED——进程生命周期 ~1s，本宿主 PS 采样（进程退出后 PeakWorkingSet64 读 0）与 50ms 轮询两种方法均无法可靠捕获；如实标注，不冒充数据。
- 事务加宽说明（R2 #7）：stage/commit/addSourceAssetsToDraft 现在在 BEGIN IMMEDIATE 内做清单解析/指纹（正确性优先），写锁窗口加宽由 busy_timeout=5000 兜底；racer 套件 cpu_avg 656ms（含 80 轮全生命周期）显示无病理性串行化代价。
