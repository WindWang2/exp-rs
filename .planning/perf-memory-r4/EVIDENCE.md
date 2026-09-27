# EVIDENCE — Track 18 perf-memory R4

> 环境声明：全程无外网；Release/GCC 16.2.1/Linux 6.18；`ctest -j1`。
> 本机 16 核与**其他并行 agent 会话共享**（期间存在外部编译负载，绝对毫秒数波动
> ±40%），因此证据以**同轮内对比、结构门、复杂度、digest 位一致**为准；跨轮绝对值
> 仅作参考并如实标注。

## 1. READINESS 收口两轮验证

- 处置直跑证据：见 READINESS_CLOSURE.md 逐项（io 系 9 件直跑 rc=0 除豁免项；heavy 件
  fuzz_ipc 3959 断言 / mapspec 2604/222 / stress 2152/6 / worker_host 61/13 全绿）。
- Ladder 全量（`scripts/verification_ladder.py --json`，-j2 构建预算）：L3/L4/L5/L7 ok，
  L2 ATTENTION = platform/command/diagnostics contract 9（快照漂移，#1337 在途）+
  io_atomic_failures（#1338 在途）——与本轨分类一致；L2 面 20 个 target 首轮 not_built
  系定向构建集之外，已补编译进入第二轮 ladder。
- Oracle 双跑：`ctest -R "io|bench|readiness|memory|perf|mapspec|fuzz" -j1`
  轮1 / 轮2 退出码见第六节（最终收口时刷新）。

## 2. profile 证据与优化（WP-C，8 处）

**Profile 方法**：临时微基准（读路径 ns/pixel、按编解码/布局分档、NDVI 流水线逐阶段
插桩、变值编码计时），全部在同轮内做 A/B；不设绝对毫秒门。

**机制测量结论（决定优化方向）**：
- 流水线被 GDAL 编解码支配：LZW tile 解码 ~14-16 ns/pixel（1-band 文件）、整 tile
  LZW 编码 ~69 ns/pixel（~60 MB/s，行写基准曾漏计时 close 造成 17× 低估，已修正）；
  无压缩 tile 读 1.5 ns/pixel、写 3.2 ns/pixel。算子内核本身 ≈ 1-4 ns/pixel。
- 多波段交错 LZW 文件单带扫描在 64MB pin cache 下 ~60 ns/pixel（容量缺失重解码）。
- **被证据否决的候选**（不做，避免为做而做）：
  a) BIP 合并读代替逐带读：实测更慢（96-101ns vs 87-92ns）——GTiff 已做共享 tile
     拆分缓存；
  b) 换 DEFLATE+PREDICTOR3：解码 −34% 但整 tile 编码 +40%（97 vs 69ns），
     写多读少场景净亏；
  c) LZW 加 PREDICTOR：解码 −11% 但编码 +20%，且改变产品可见元数据；
  d) bandNoDataValue memoization：占热路径 ~0.07%，不值得引入缓存失效复杂度。

| # | 位置 | 改动 | 证据（前→后） | 行为不变 | 提交 |
|---|---|---|---|---|---|
| 1 | gdal_dataset_wrapper readBandWindow | 内部窗口跳过冗余 pad 预填 | fill-only 0.10ns/pixel ≈ 无压缩块读 1.5ns 的 7%，纯浪费 | guard sums 位一致 | 28d77be7de |
| 2 | gdal_dataset_wrapper readWindowBip | 同上（NaN 预填） | 同上 | 同上 | 28d77be7de |
| 3 | rs_recode 逐像素映射 | QMap 树查找→平面向量线性扫 | 机制：每像素 O(log n) 树走 ×2 遍；整算子 712-800→772-865ms（负载噪声内） | test_post_process 76/11 | 517e3e7bab |
| 4 | rs_recode pass2 | int 块→typed 块的二遍拷贝融合进映射单遍 | 每 block 少一整遍写流量 | 同上 | 517e3e7bab |
| 5 | rs_spectral_index readBlock | nodata+scale 两个归一化遍融合 | stage 计时 norm 50.3-50.6ms→44.4-49.6ms（方向跨轮一致） | test_rs_operators 9041/79 | b511822262 |
| 6 | rs_majority_filter | 每块两次堆分配提出循环 | 机制：块循环内 2 次分配→0 | test_post_process 76/11 | dff8da78c3 |
| 7 | rs_qa_mask | unknown-OR 与 masked-count 两遍合一 | 机制：同数组两遍读 | test_rs_operators 9041/79 | 6682328f54 |
| 8 | rs_change_streaming | 每带归一化遍与 BIP scatter 融合 | 机制：每带每 tile 少一整遍；guard sum 位一致 | test_rs_operators 9041/79 | 6b76b3460b |

**RECOMMENDED（有数据、属产品策略，不在本轨擅改）**：DEFLATE+PREDICTOR3 适合
读多写少、可接受写侧 +40% 的场景（解码 −34%）；无压缩适合分析就绪中间件
（写 3.2ns、读 1.5ns）。数据见本轮 profile。

## 3. 拷贝消除（≥2）

即第 2 节 #1/#2（band block 通道冗余预填消除），digest 位一致判定。

## 4. 缓存正确性用例

本轨全部落地优化**不引入缓存**（候选 d 被证据否决且理由入 DECISIONS），故
"缓存失效正确性测试"按 3.2 条件条款不适用；已显式记录于此。

## 5. 内存守护（WP-D，双跑）

| 守护 | 输入 | 峰值 RSS 增量（轮1/轮2） | 声明界 | 结果 |
|---|---|---|---|---|
| guard_tiled_inference_rss | 4096² F32 tile256 cache64MB | 66 / 65 MB | 96 MB | 双过 |
| guard_temporal_composite_rss | 6×4096² F32 | 76 / 76 MB | 352 MB | 双过 |
| guard_change_detection_rss | 2×4096² F32 | 68 / 67 MB | 97 MB | 双过 |

- 输出像素和（Kahan）跨双跑**逐位一致**（composite 8388725632.921366、change
  5592760990.412545），优化系列后复测仍一致 → 行为不变的外部证据。
- 诚实边界：守护在共享进程（全用例单进程跑）下会读到跨用例 GDAL 缓存残留
  （composite 曾 580MB vs 隔离态 76MB）；ctest 逐用例独立进程为正式执行模型。

## 6. WP-F Oracle 双跑（`ctest -R "io|bench|readiness|memory|perf|mapspec|fuzz" -j1`）

- 注册面终态 1090 项。轮1（F1）：**1044 绿 / 46 红**；轮2（F2）：**1044 绿 / 46 红**；
  两轮失败集合**逐项相同（零 flake）**。终轮 F3（补建 4 个漏建目标后）：**1048 绿 / 42 红**（-4 = 4 个补建件转绿；
红集与 F1/F2 的 20 Failed 逐项相同 + 22 Not Run 占位）。
- 46 红 = 20 Failed + 26 Not Run，全部带归属、零悬置：
  * Failed 20：VectorWriter/issue850/组发布/io:vector_convert/geometry collections（6，
    = #1338 在途修复族）；mission runtime×2、classification finalize×2、detection
    ensemble、prepared decisions、projection block、Resume re-executes、Cancellation
    hook、D19 hermetic、experiment retention×2（12，= #1335 报告的 142 既有失败分类，
    其中多类由 #1337 在途修）；F5 failure_11（1，新记录既有项）；ebench remote cog
    read Timeout（1，#1335 Group6 loopback 端点双拼前缀类，#1338 R2 在途）。
  * Not Run 26：21 个 master 既有编译破损目标的占位（test_workbench_enum_provider、
    test_verification_env_12、test_classification_* 等——#1335 在旧 master 修过部分，
    现行 master 再回归，全部在本轨白名单外，逐个记录于 ladder 日志）；5 个漏建目标
    （edit_annotation/edit_session/phenology_extraction/virtual_cube_memory/…）中 4 个
    本轨补建后**双跑全绿**（407/29/120/60 断言），obia×2 为既有破损。
- **本轨所属面 100% 绿**：io URI/paths/stac/grid/raster/remote×3、fuzz_ops/fuzz_ipc、
  fault matrix/fault injection、exprs_ipc、concurrency stress、worker host、mapspec、
  bench×3、memory guards×3、obs op×6、rs_operators（9041 断言）、post_process、
  perf_observatory 全家。
- digest 位一致终证：guard output_sum 双轮逐位相同（inference 16776725747.400202、
  composite 8388725632.921366、change 5592760990.412545）。

## 7. WP-E 基线扩展

6 项 obs_op_*.json + baseline md 索引更新 + quality7.json 重生成（见提交
e0ad3ce07f；ctest 双跑 6/6 结构门全绿）。
