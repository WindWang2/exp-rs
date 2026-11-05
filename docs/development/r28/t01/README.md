# R28-T01 — 遥感算法数值可靠性、采样内存与重复发布（2026-11）

本轨在现有算法家族上完成六项修复与两组验收证据，不引入新平台/新 registry。
基线 `50cc3255c5`（= origin/master @2026-11-04）；全部证据在 `.planning/r28-t01-science/`。

## 用户可见变化

### 1. 训练采样内存：特征矩阵按"保留样本"而非"候选像元"分配
`RsTrainingDataExtraction`（ROI/训练向量 → X/y）改为两遍扫描线：
第一遍只记录每个候选像元的 NoData/忽略命中（逐波段可分解，与
`RsPixelIgnoreOptions::isIgnorePixel` 逐位等价），第二遍只读仍含保留样本的行。
`out.X` 从 **候选数 N × 波段 B × 4 字节** 收敛到 **保留数 K × B × 4 字节**
（100 Mpx × 3 波段场景的主矩阵从 ~1.2 GB 降到 cap 决定的小矩阵）。

契约不变（全部有钉值测试）：overlap last-class-wins、无效像元不消耗每类 cap
（R26-S2）、(class, pixel) 升序行序、mt19937(42) + 便携 Fisher-Yates 确定性、
取消语义、sampleGroupIds 对齐。代价：候选行每波段最多读两遍（第二遍只读含
保留样本的行）。剩余 O(N) 部分如实保留：pixelClass/pixelGroup 哈希、
samples/byClass/shuffle rank（确定性契约的载体；内存曲线见
`.planning/r28-t01-science/PERF_BASELINE.md`）。

### 2. 变化检测同参数二次运行（执行缓存命中）不再失败
`rs:change_detection` 带 `makeMask` 时，同一输出参数在同一进程/共享工作目录下
第二次运行会命中分块执行缓存（journal published 标记）。此前 mask 阶段尝试
reopen 本轮从未创建的 magnitude 临时文件而必然失败（r26 遗留附录问题）。
现在 mask 栅格在写入时持久化完整统计
（`SICNU_CHANGE_{MEAN,STDDEV,MIN,MAX,CHANGED,EVALUATED,THRESHOLD}`，
`'g',17` 精确往返），缓存命中轮验证方法一致后读回并返回与首轮一致的结果
（`already_published: true`）。缓存标记与输出不一致时给类型化错误并指明
处置（删输出+journal 或换输出路径），不再产出误导性的 "reopen magnitude" 失败。

### 3. 掩膜链保住定标契约（#1740/#1755 的链路缺口）
`rs:apply_mask` 现在把 `SICNU_NUMERIC_SCALE/OFFSET`（band 级 + 数据集级）
随产品元数据一起传递。此前 `qa_mask → apply_mask → spectral_index` 链在掩膜
一步丢掉 offset 章，比值指数重新出现 #1755 偏差（offset DN 上 NDVI 0.5 而非
物理 1/3）。

### 4. 光学二次定标显式拒绝
`rs:radiometric_calibration` 现读取输入的 `SICNU_RADIOMETRIC_STATE`：已处于
物理态（radiance/toa_reflectance/surface_reflectance/brightness_temperature）
的输入被拒绝并说明原因（典型场景：输出落在原始 MTL 旁，自动探测再次套用
DN 系数）。`digital_number` 态与未盖章栅格正常放行。与 SAR 定标、大气校正
的既有守卫对齐。

### 5. 奇异端元系统解混给 NaN 而非静默 0
OLS 光谱解混在 Gram 矩阵 ridge 后仍奇异时，此前输出全 0 丰度 + 伪装的
"像素范数"重构误差；现在按模块既有的"不可比较"约定输出 NaN 丰度与 NaN
误差（FCLS 路径本就拒绝同类系统）。

### 6. rs:band_math 输出质量摘要
结果 JSON 新增 `valid_pixels` / `nonfinite_output_pixels` /
`valid_pixel_ratio`，来自实际逐像元求值（除零、掩膜输入产生的 NaN 可见），
与 `rs:spectral_index` 的质量字段同一约定。

## 复核关闭的 issue（修复由 #1807 引入，本轨逐子项复核 + 锚点重跑）
#1738 #1739 #1740 #1752 #1753 #1754 #1755 #1756 #1757 #1758 #1759 #1801
（逐项证据：`.planning/r28-t01-science/ISSUE_TRIAGE.md`）

## 验证
- 新增 `tests/test_r28_t01_science.cpp`（11 用例：A1-A4 采样契约、B1-B2 重复
  发布+统计 oracle、C1-C4 定标链/二次定标/解混/band_math、D1 分类重载一致性）
  与 `tests/test_r28_t01_memory_bench.cpp`（SLOW lane，报告式内存曲线 S1-S4）。
- RED 取证：基线 `50cc3255` 上 B1/C1/C2/C3/B2 失败（对应六项缺陷），变更构建
  全绿；兄弟套件（training_data_extraction / r26_science_oracles /
  r27_t01_science_quality / classification_pipeline / classification_e2e /
  jm_separability）两遍全绿。
