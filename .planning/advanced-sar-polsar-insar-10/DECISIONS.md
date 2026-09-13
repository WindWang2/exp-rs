# DECISIONS — advanced-sar-polsar-insar-10

格式：`D-N · 决策 · 理由 · 备选`。全部由主代理在 autonomy=full 下裁定。

## D-001 · 解缠策略：内建参考实现（quality-guided flood fill）+ 外部 provider 升级路径
- 决策：`rs:sar_unwrap` 内建一个确定性 quality-guided（相干性优先）flood-fill 解缠参考实现，
  文档明确其局限（对残差点密集数据不保证全局最优；不是 SNAPHU/MCF 级），同时在内核接口
  上保留 `provider` 参数位：显式外部 provider 名在未注册时 typed refusal（错误码
  `UNWRAP_PROVIDER_UNAVAILABLE`），绝不静默退回参考实现。
- 理由：验收标准允许"接口/adapter 或明确外部 provider contract"；仅有 contract 则基础链
  端到端不可执行（gate 5 要求链条跑通）。quality-guided flood fill 在 <π 梯度合成数据上
  可精确闭合，已知答案可测；O(n log n) bounded。
- 备选：纯 provider contract（链条断）；DCT 最小二乘（需处理残差加权，复杂度高）。

## D-002 · PolSAR 输入契约：CFloat32 波段 + 通道身份 metadata，双表达
- 决策：全极化输入 = 单栅格内 3 或 4 个 CFloat32 波段（复散射系数），通道身份按
  `SICNU_SAR_COMPLEX_CHANNELS`（如 `HH;HV;VV`）声明，算子另接受显式
  `hh_band/hv_band/vh_band/vv_band` 参数覆盖；reciprocity（SHV=SVH）仅当声明 3 通道时成立。
  4 通道且 HV≠VH 时提供 `assumeReciprocity` 参数（默认 true→仅取 HV 并记录；
  false→类型化拒绝 `NON_RECIPROCAL_CHANNELS`）。
- 理由：Track 02 拥有产品拆包；本 track 只消费通用 GDAL 栅格。双表达兼顾 agent 自动化
  （metadata 可发现）与显式管道（band 映射参数，与 `rs:sar_dualpol_features` 的
  vv_band/vh_band 模式一致）。
- 备选：仅 metadata（headless 管道难用）；仅参数（agent 无法自描述）。

## D-003 · PolSAR ensemble：窗口空间多视内建为参数
- 决策：单视 SLC 的 T3/C3 秩为 1（H≡0），分解需集合平均。算子暴露 `windowSize`
  （奇数，默认 5）做 boxcar ensemble，逐像素局部协方差在窗口内累积；文档声明
  "窗口平均 = 分解有效分辨率下降"。
- 理由：无 ensemble 的 H/A/α 数学上退化（H=0 恒成立），实现之即造假能力。
- 备选：要求外部多视（把负担转嫁给调用方且平台无 multilook 算子）。

## D-004 · 3×3 Hermitian 特征分解：内建 Jacobi（复 Givens）闭式实现
- 决策：`sar_hermitian3.{h,cpp}` 实现 3×3 Hermitian 的循环 Jacobi 特征分解（复平面
  Givens 旋转，固定收敛阈值 + 迭代上限，确定性固定扫描顺序）。不引 Eigen。
- 理由：H/A/α 与特征稳定性诊断是核心需求；OpenCV `cv::eigen` 仅实对称；新依赖被
  autonomy default 7 禁止。3×3 规模下 Jacobi 快速收敛且已知答案可测（构造特征对）。
- 备选：OpenCV 实化技巧（把 Hermitian 嵌入 6×6 实对称——数值上等价但两倍开销且绕）。

## D-005 · InSAR coregistration seam：同网格硬契约 + 独立精配准算子
- 决策：`rs:sar_interferogram` 要求主/从景同网格（CRS、尺寸、geotransform 逐项相等），
  否则 `GRID_MISMATCH` 拒绝（与 `rs:sar_change` 一致）；单独提供
  `rs:sar_coregister`：对复 SLC 对做幅度域 patch 归一化互相关 → 整数峰 + 抛物线亚像素
  精化 → 全局平移模型 → 复数双线性重采样。不做多项式/DEM 畸变模型（honest subset）。
- 理由：完整 coregistration（倾斜/形变场）超出"基础链"；同网格契约复用 #929/#938 建立的
  语义；平移模型覆盖配准误差主项且合成数据可精确测试。
- 备选：内建完整多项式配准（范围爆炸、风险高）。

## D-006 · 平地相位去除：可选一阶/二阶多项式 ramp 估计（最小二乘，robust 权重）
- 决策：`rs:sar_interferogram` 提供 `flattenRamp` 参数（none|linear|quadratic）：
  对干涉相位做稳健（IQR 裁剪迭代）多项式最小二乘并减除；结果 JSON 报告拟合系数与
  残差 RMS。不从 orbit+DEM 做严格平地（那是 geocoding 契约的自然扩展，留给 follow-up）。
- 理由：合成测试可控（已知 ramp 精确恢复）；严格平地需要轨道+DEM 联合采样，
  本期以 honest polynomial 近似并如实命名（不声称 "topographic phase removal"）。

## D-007 · 时序事件语义：新算子 `rs:sar_temporal_events`，不改既有固定波段序
- 决策：`rs:sar_temporal_stats` 的固定波段序（`SICNU_SAR_TEMPORAL_BANDS`，sar-domain.md
  §5）不动；时间语义在新算子 `rs:sar_temporal_events` 实现：波段
  event_flag/first_event_index/last_event_index/event_count/max_deviation_db +
  `argmax/argmin days-since-first-acquisition`；结果 JSON 携带 `dates[]`（ISO8601，
  场景顺序）与逐波段语义描述。acquisition dates 来源：参数 `dates:[...]` 显式给出，
  或各景 `SICNU_SAR_ACQUISITION_UTC` metadata；缺declared 时 typed refusal
  `ACQUISITION_DATES_MISSING`（不再允许无语义输出——S-1 的正面关闭）。
  `rs:sar_temporal_stats` 仅 additive：结果 JSON 增加 `dates[]` 回显（当参数/metadata
  可得时），band 输出与既有契约逐字节不变。
- 理由：S-1 的根因是"index 无日期语义"；固定契约文档（sar-domain.md §5）明确波段序，
  改动会破坏已发布契约与判分锚点。
- 备选：改 `rs:sar_temporal_stats` 波段序（破坏契约）；只改 JSON（band 仍是裸 index）。

## D-008 · 时间解析与不规则间隔
- 决策：ISO8601 (`YYYY-MM-DD[THH:MM[:SS[.mmm]]Z]`)，解析后转 double 秒（UTC epoch）；
  拒绝非升序 dates（`DATES_NOT_ASCENDING`）；间隔不规则是显式支持场景——
  所有统计基于实际时间差（days 浮点），不假设等间隔。
- 理由：SAR 重访本就不规则（S1 12 天/6 天混合）；等间隔假设是光学时序的常见错误，
  不带入 SAR。

## D-009 · 相位→位移的诚实质量门
- 决策：`rs:sar_displacement` 要求 `wavelengthUm`（或场景 `SICNU_SAR_WAVELENGTH_UM`
  metadata）；输入必须是已解缠相位（文档约定），算子内部计算 Itoh 不连续计数
  （|Δφ|>π 像素对比例）写入结果 JSON `phaseDiscontinuityRatio`，超过阈值（默认 0.02）
  不失败但在 JSON 与 stderr 警告——最终判定交给调用方；d_los = −λ·φ/(4π)。
- 理由：无法从数据可靠区分"wrapped vs unwrapped"；诚实计数 + 警告优于硬拒绝
  （部分有效场景合法存在跳变边缘）也优于静默。

## D-010 · 确定性分级
- 决策：所有新内核 bit-exact（逐像素纯函数或固定顺序规约）；窗口 ensemble（PolSAR boxcar、
  InSAR 相干性窗口、Goldstein 滤波）为固定扫描顺序单线程规约 → bit-exact；多线程仅在
  tile 级并行（tile 间无数据依赖）时使用，本轮实现为单线程顺序 tile 循环（与
  GdalBlockStream 线程契约一致），grade 声明 bit_exact。
- 理由：平台 Determinism Grade 语义（CONTEXT.md）；单线程 tile 循环是
  `GdalMultibandBlockStream::forEach` 既有契约。

## D-011 · 基线几何（B⊥/B∥）append 进 `sar_orbit.{h,cpp}`
- 决策：干涉基线几何（给定 master/slave 状态向量与视线方向：B⊥、B∥、临界基线公式）
  作为 `sar_orbit.h` 的 append 扩展（`sar_baseline` 独立头文件 include 它），不动既有函数。
- 理由：sar_orbit 是 SAR-specific（本 Track ownership）；WGS84/ECEF/LOS 基础设施就地复用。

## D-012 · Yamaguchi 四分量采用含 helicity 的标准形式（2012 refined 版本语义）
- 决策：实现 Yamaguchi 四分量（surface/double/volume/helix），volume 分支按
  orientation angle=0 的标准形式；文档引用与假设（反射对称性不成立时的 helix 分量语义）
  写进 sar-domain.md 追加章节。
- 理由：验收标准写明"如科学/数据条件允许"；窗口 ensemble 后的四分量功率闭合
  （SPAN = 四分量之和）作为已知答案测试锚点。

## 评审后修订（Phase 7，subagent A 输入后）

- **D-002 修订（波段映射参数命名）**：实现与 docs §6.2 采用 camelCase
  （`hhBand/hvBand/vhBand/vvBand`），与原决定"沿用 dualpol 的 snake_case"不符。
  保留 camelCase：本算子参数多于 dualpol（4 个波段 + assumeReciprocity），
  camelCase 与 schema 其余参数一致；docs/代码/能力页三方一致。原 snake_case
  提议作废，记录为已修订决定。
- **D-004 论证补全**：仓内已有 `ImageEnhancement::jacobiEigen`
  （image_enhancement.h:99）为 float 实对称 Jacobi——复 Hermitian 需要复
  Givens 旋转与复特征向量累积，不能直接复用；`sar_hermitian3` 不构成重复
  实现，依据补记于此。
- **D-006 兑现**：interferogram 结果 JSON 现报告 `rampCoefficients` 与
  `rampCoefficientOrder`（原承诺的拟合系数）。
- **D-011 状态**：`interferometricBaseline`（B∥/B⊥/|Δr|，unit-LOS 校验）
  已实现于 sar_orbit.{h,cpp}，known-answer 测试钉住（test_sar_orbit.cpp）。
