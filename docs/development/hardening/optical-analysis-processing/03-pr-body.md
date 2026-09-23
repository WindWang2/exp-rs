# PR body — hardening/optical-analysis-processing

**Title**: `fix(processing): optical analysis hardening 16/20 — degenerate band fallback, mask grid contract, alias identity, label cast guards, extract provenance, feature-stack validation, guard close-order, band-math cancel, IHS NaN`

## 目标

本 track 限定既有光学遥感分析/处理模块的科学正确性强化:不允许新产品方向。本轮在
master `a9dc33fa7` 上经源码级 review + 独立 recon 扫描确认了 2 个 P1 与 8 个 P2
缺陷,全部以回归 oracle 先证明、再最小修复。

## Recon 基线

- master `a9dc33fa7`(与启动时 recon seed 一致);open issues 0;open PRs #1237–#1244。
- 现状矩阵与逐文件加固成熟度:`docs/development/hardening/optical-analysis-processing/01-recon.md`。
- 与 open PR 的去重:#1243(spectral_resampling/detection streaming)、#1244(temporal)、
  #1242(geospatial fabric/STAC/raster_reader)、#1237(teaching shell)的文件面零交集。
- 旧分支 `agent/flash-processing-atomic-errors` 仅作线索矿;其写可见性问题已被 master 的
  atomic_fs 体系(#617/#1216/#1224)取代,未移植任何代码。

## 根因与修复(每个修复绑定一个 RED→GREEN oracle)

| 缺陷 | 根因 | 修复 | Oracle |
|---|---|---|---|
| **[P1] NBR/NDRE/UI/NDTI 在缺角色元数据的栈上输出全 0 常量** | `positionalFallbackBand` 对 swir2/red_edge 做 `min(6,n)`/`min(5,n)` 钳制,回退带与 nir 重合后 `validateBand` 无法拦截,归一化差变成常量 0 且 exit success | rs:spectral_index(含全部 alias)在解析后对**参与角色**做重复带检查:重复且至少一侧来自解析器回退 → 拒绝;双侧显式保持放行 | test_rs_operators `…degenerate positional duplicate bands…`(4 波段 NBR 抛"degenerate all-constant ratio",NDVI 正控不受影响) |
| **[P1] apply_mask 单侧缺 geotransform 时位置对齐静默应用** | `compareGrids` 在 CRS 相同但任一侧缺 GT 时返回 compatible(契约说"callers fall back to dimension checks"),算子只对双缺 GT 检查维度;8×8 无 GT mask 套 4×4 输入时取左上窗成功写出 | fallback 条件扩展为"任一侧缺 GT"→ 维度不等即拒;单侧缺 GT 时记录 position 应用说明 | test_rs_operators `…dimension contract when georeferencing is missing on one side…` |
| [P2] facade alias 认可未声明的 `index` 参数(rs:ndvi 可产 MNDWI) | `runSpectralIndexCore` 无条件接受 `index`,而 job/batch 执行器不做 schema 校验 | 增加 `allowIndexOverride`(仅 generic 算子为 true);alias 收到冲突值 → 拒绝;恒等值放行 | test_rs_operators `…alias operators refuse a conflicting index override…` |
| [P2] extract_bands 丢弃 NoData/role/wavelength/辐射态/numeric scale | 输出为全新 GdalStreamingOutput,仅拷贝 GT+投影 | 逐波段传播 NoData/SICNU_BAND_ROLE/WAVELENGTH/FWHM,数据集级传播 SICNU_RADIOMETRIC_STATE/SICNU_NUMERIC_SCALE(GdalStreamingOutput 新增 `setBandMetadataItem`) | test_rs_operators `…extract_bands carries NoData, band roles and provenance stamps…` |
| [P2] recode 对 float label 直接 cast:哨兵 → UB(INT_MIN 伪类) | `static_cast<int>(float)` 越界是 UB;x86 cvttss2si 得 INT_MIN 并写入交付物 | 非有限/超 int 域 → InvalidInputData("not an integer class",指引先掩膜);pass1 在输出创建前抛出,无部分文件 | test_rs_operators `…recode refuses non-integer label values…` |
| [P2] majority filter 边界:`fv <= float(INT_MAX)` 中 `float(INT_MAX)=2^31` 恰被放行 | `static_cast<float>(2147483647)` 向上取整为 2^31 | 上界改排他 `< 2147483648.0f` | test_rs_operators `…2^31 float boundary as non-voting…` |
| [P2] feature_stack 接受 scale=0/NaN:整波段被换成 0 且契约声称 scale=1 | scale/offset 未验证;契约无条件记 scale=1/offset=0 | scale 必须有限非零、offset 有限,否则 InvalidParameter | test_rs_operators `…feature_stack validates scale/offset…` |
| [P2] feature_stack 用 WKT 字符串相等比较 CRS | 绕过了共享语义接缝 | 改 `sicnu::data::isSameCrs`(OSRIsSame);空 CRS 行为不变(仍走 warning) | test_rs_operators `…compares CRS semantically…`(正向控制:本平台 GDAL 导出将编码归一化,RED 不可稳定构造,诚实降级并注明) |
| [P2] PartialOutputGuard 逆序析构先删后关 GDAL 句柄:Windows 共享违规 → fail-open | 成员声明序依赖,析构先于 dataset 关闭 | guard 增加 `setCloseFirst()`(触发时先调用,再删除);4 个调用点(mnf/mnf_inverse/unmixing/band_select)注册 | test_rs_operators `…closes registered datasets before removing paths…` |
| [P2] band_math 求值无取消探针 | `processFile` 无 isCancelled 通道,算子只在头尾检查 | 每块探针;取消时 errorMessage="Cancelled"、部分输出由流式输出析构清除;算子映射为 `Cancelled` | test_rs_operators `…BandMath::processFile honours the cancellation probe…` |
| [P2] in-memory `ihsFusion` NaN 输入 → 黑像素 | 只做 `== nodata` 比较;NaN 流过 IHS 数学后被 `std::max(0.0f, NaN)` 折叠为 0;与流式路径及全部兄弟内核不一致 | 三处循环补 `isnan` 门(哨兵与 NaN 两种 NoData 约定) | test_image_fusion `…NaN NoData pixels stay NoData instead of collapsing to black…` |

## 科学语义依据

- NBR ≡ 常量 0 意味着"完全燃烧严重度",这是科学上的伪造结果;4 波段栈本无 SWIR2 波段,
  失败关闭优于静默输出。
- float NoData 哨兵(-3.4e38/-9999)进入 int 域是分类交付物中的伪类;标签栅格按定义是整型域。
- NoData/角色/波长/辐射态/numeric-scale 是校准→指数链路的科学状态载体(#680/#801/ADR 0114);
  extract_bands 作为纯拷贝必须保持该状态,否则下游按"未声明"处理哨兵与 DN 域。
- IHS 的 NaN 折叠为 0 与 #699 之后"输出 NoData=NaN"的全链路约定冲突:黑像与真实暗像不可区分。

## 文件清单

src/operators/rs/{rs_spectral_index_operator,rs_apply_mask_operator,rs_recode_operator,
rs_majority_filter_operator,rs_feature_stack_operator,rs_band_math_operator}.cpp,
src/operators/rs/rs_{mnf,mnf_inverse,spectral_unmixing,spectral_band_select}_operator.cpp,
src/operators/rs/rs_partial_output_guard.h,
src/processing/algorithms/{band_math.cpp,band_math.h,band_tools.cpp,image_fusion.cpp},
src/processing/gdal/gdal_multiband_block_stream.{h,cpp},
tests/{test_rs_operators,test_image_fusion}.cpp,
docs/development/hardening/optical-analysis-processing/{01-recon,02-test-ledger,03-pr-body}.md

## 测试证据

- 构建:Debug,`-DENABLE_TESTS=ON`,`-j2`;新增/修改 TU 无新增 warning(见 ledger)。
- 新增 11 个回归 oracle(标签 `hardening16`);关键 oracle 连续两遍通过(见 ledger 运行记录)。
- 既有近邻 suite 全绿:test_band_math(差分 oracle)、test_radiometric_calibration、
  test_atmospheric、test_feature_cube 等不受影响(targeted 运行,见 ledger)。

## 性能/资源

本轮以正确性为主,无性能重写。band_math 取消探针为每块 O(1) 谓词调用;extract_bands
元数据传播为每输出波段 O(1) GDAL 调用;spectral_index 参与角色检查为解析期 O(r²)(r≤7,
与每像素开销无关)。

## 独立 review

独立 reviewer(未参与实现)完成逐 claim 验证 + 实证 RED(将 master 实现文件换入重建):
master 语义下 10 个新 oracle 中 7 个失败 + image_fusion NaN case 2 处失败,证明各 oracle
确实钉住旧缺陷;分支恢复后全数复绿。**结论 READY,无 P0/P1**。发现并已修复:
P2-1 CRS 测试注释失实(本平台 GDAL 导出归一化使旧字符串比较在该构造下不触发,已把该
case 降级为正向控制并在测试内注明)、P3-1 spectral-index guard 消息 side-neutral、
P3-2 apply_mask 错误消息 side-neutral、P3-3 recode 守卫单次调用、P3-4 recode 错误文案。
Reviewer 另核清:guard 先于输出创建、setCloseFirst 无 double-close/UAF、取消后无部分输出、
显式-显式等带参数豁免、空 CRS 行为不变。

## 已知限制(准确记录,不模糊 TODO)

- 已被其他 track 拥有:spectral_resampling/detection(#1243)、temporal(#1244)、
  fabric/raster_reader/STAC(#1242)、teaching shell(#1237)。
- 无法复现:flash-processing-atomic-errors 写可见性(已被 master atomic_fs 覆盖)。
- 需要真实平台环境:OTB CLI 实测路径(engine=otb 依赖 SICNU_OTB_PATH,契约由既有 stub 测试覆盖)。
- 明确未来方向:ISSUES.md 登记的算子缺口(H-2/H-3 等)属产品方向,不属缺陷。
- P3 余项(apply_mask floor/round 半像素、threshold 参数验证、change-mask ±inf、recode
  schema 双形态、contrast_stretch 参数验证、getInt 截断、#612 注释漂移、死前向声明、
  quality-report 失败时输出保留语义)——逐条记录于 02-test-ledger.md。

## 回滚方案

单 PR 回滚即可;无 schema/major 版本变更,无数据迁移。所有新参数校验为 fail-closed,
旧调用方可通过补齐显式参数恢复旧行为。
