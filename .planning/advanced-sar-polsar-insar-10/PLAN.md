# PLAN — advanced-sar-polsar-insar-10

架构蓝图（细节契约以 `docs/processing/sar-domain.md` 追加章节 + 头文件注释为准）。

## 新内核模块（src/processing/algorithms/sar/）

| 文件 | 内容 |
| --- | --- |
| `sar_complex.{h,cpp}` | 复通道契约：`SICNU_SAR_COMPLEX_CHANNELS` 解析；CFloat32 窗口读取（`GdalDatasetWrapper::readWindowNative`）、CFloat32 tile 写（`GdalStreamingOutput::writeTileRaw`）；amplitude/phase/power 提取；逐通道 sentinel→(NaN,NaN) 策略；complex BIP tile 流（复刻 GdalMultibandBlockStream 形状） |
| `sar_hermitian3.{h,cpp}` | 3×3 Hermitian 循环 Jacobi 特征分解（确定性、降序特征值）；实 3×3 对称为退化路径 |
| `sar_polsar.{h,cpp}` | PolSAR 通道模型（full-pol 3/4 通道 + reciprocity）；Pauli 基；窗口 ensemble T3/C3；H/A/α（Cloude-Pottier）；特征稳定性（λ 比/条件数）；Freeman-Durden 三分量；Yamaguchi 四分量（helicity）；SPAN 闭合 |
| `sar_insar.{h,cpp}` | SLC 配对 preflight（网格/极化/复型/时间可比性）；干涉图 + 相干性（窗口）；平地 ramp（稳健多项式）；Goldstein-Werner 相位滤波；quality-guided 解缠（D-001）+ provider 参数位；LOS 形变（−λφ/4π）+ Itoh 不连续计数 |
| `sar_temporal_events.{h,cpp}` | ISO8601 acquisition-date 契约解析；事件核（阈值 crossing、first/last、count、max dev、argmax/argmin days-since-start）；不规则间隔（实际时间差）；缺失景（NaN 采样 + valid bookkeeping） |
| `sar_orbit.{h,cpp}` append | 干涉基线几何：B⊥/B∥、临界基线（给定两平台状态 + LOS 单位向量） |

## 新算子（src/operators/rs/，注册 rs:sar_*）

| 算子 | 输入→输出 |
| --- | --- |
| `rs:sar_polsar_decompose` | CFloat32 3/4 通道 → pauli/h_alpha/freeman_durden/yamaguchi/span 多波段 Float32 + 物理含义 metadata |
| `rs:sar_interferogram` | 主/从 CFloat32 SLC（同网格）→ phase(wrapped)/coherence/amp_master (+ramp flatten) |
| `rs:sar_phase_filter` | 相位+相干性 → Goldstein 滤波相位 |
| `rs:sar_unwrap` | 滤波相位(+quality) → 解缠相位 |
| `rs:sar_displacement` | 解缠相位 → LOS 形变 (m) + 质量诊断 |
| `rs:sar_coregister` | SLC 对 → 互相关偏移 + 复数重采样从景（D-005） |
| `rs:sar_temporal_events` | N 景 detected stack + dates → 事件波段 + dates[] JSON |

既有算子 additive：`rs:sar_temporal_stats` 结果 JSON 增 `dates[]` 回显（band 输出不变）。

## 测试（tests/）

| 文件 | 覆盖 |
| --- | --- |
| `test_sar_complex.cpp` | CFloat32 GTiff 往返、通道声明解析、sentinel、相位/幅度已知值 |
| `test_sar_polsar.cpp` | 构造散射矩阵已知特征分解（球/二面角/偶极子/螺旋体）；FD/Yamaguchi 分量闭合；reciprocity/refusal 矩阵；各向异性网格 |
| `test_sar_insar.cpp` | 已知相位 ramp SLC 对 → 干涉相位精确；同景相干=1；Goldstein 无噪声不变性；解缠 <π ramp 精确、residual 计数；位移闭式；ramp 去除；refusal 矩阵；cancellation |
| `test_sar_temporal_events.cpp` | 日期语义、不规则间隔、缺失景、refusal（缺日期/乱序）；与 sar_temporal 一致的域规则 |

既有：`test_sar_kernels`/`test_sar_operators`/`test_sar_orbit`/`test_sar_geocoding`/`test_sar_temporal_stats` 回归不动。

## 集成落位

- `rs_operators_init.cpp`：REGISTER + factory append（integration commit）。
- `src/operators/CMakeLists.txt`：源文件 append（同一 integration commit）。
- sidecar：新算子跑 `capability_knowledge_tool gen-meta` → authored enrichment（中文 summary/
  applicability/teaching/失败模式）→ `gen-pages` 重生成 → `gen-pages --check` 零 drift。
- `docs/processing/sar-domain.md`：追加 §6 complex/SLC、§7 PolSAR、§8 InSAR、§9 temporal events。

## 批次顺序（每批次可编译可测试）

1. `.gitignore` + planning（本次 commit）
2. WP-A：sar_complex + test → commit
3. WP-B：hermitian3 + polsar 内核 + test → operator → commit
4. WP-C：insar 内核 + test → operators（干涉/滤波/解缠/位移/配准）→ commit
5. WP-D：temporal_events + test → operator + stats additive → commit
6. WP-E：注册 integration commit + sidecar/knowledge/docs + scale/edge → commit
7. Phase 7 review → 修复 commits → Phase 8/9
