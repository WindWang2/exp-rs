# R18-A PERFORMANCE — Oracle-C 基线与对照

机器：Linux x86_64（40 核，构建用 -j2），62 GiB RAM。
构建：`build-r18-a` Release。
方法：同输入、同配置、≥3 次重复，热/冷缓存分开；峰值 RSS 用 `/usr/bin/time -v` 或
`resource_guard.py` 采样；产物 hash（sha256sum）做输出 parity。

## 基线（待测量）

| 场景 | 输入 | 命令 | 时延(热/冷) | 峰值 RSS | 产物 hash |
|---|---|---|---|---|---|
| （WP9 前测：多分辨率分割器 512²/2048²/4096² 若干波段） | | | | | |
| （WP10 前测：band_math SIMD 大表达式 1M/16M 像素） | | | | | |
| （WP10 前测：quac 内存版 vs streaming） | | | | | |

## 改动后对照（待测量）

（每行：改动 | 输入相同 | Δ时延 | ΔRSS | 输出 parity | 结论）

规则：任何变慢必须解释原因或撤回；不声称未测量的加速比。

## WP9 多分辨率分割器 SoA（2026-10-19 实测）

方法：仓外 scratch 基准 /tmp/r18a_bench_mrs.cpp（合成噪声场景，scale=75, wShape=0.3,
MMU=8, bandCount=4），python `resource.getrusage(RUSAGE_CHILDREN)` 采样峰值 RSS，
每配置 2 次（热缓存；同进程冷/热差异 <2%）。构建同为本轨 build-r18-a（Release）。

| 场景 | 基线（commit 77802cf37） | SoA（285757a2f） | ΔRSS | Δ时间 |
|---|---|---|---|---|
| 512×512×4 | 186.0/186.1 MiB；990/942 ms | 166.3 MiB；881/839 ms | **−19.8 MiB（−10.6%）** | −9% |
| 1024×1024×4 | 427.3 MiB；4305 ms | 346.4/347.0 MiB；4186/3847 ms | **−80.5 MiB（−18.8%）** | −8.5% |

输出 parity：两尺寸 `segments` 与全图 label 校验和（labelsum=262144/1048576）与基线
**逐位一致**；OBIA 14 用例 ×2 全绿（含单调粗化与 NoData 拓扑 pin）。

内存模型注：每像元状态数据本身（2×B×8B）是算法固有，SoA 消除的是每像元 2×
（vector 头 24B + malloc 开销）与分配器抖动，并改善局部性；空像元不再占用任何 moment 存储。
