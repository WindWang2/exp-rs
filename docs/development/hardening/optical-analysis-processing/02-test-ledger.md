# Test Ledger — optical-analysis-processing (16/20)

Baseline: master `a9dc33fa7`, branch `hardening/optical-analysis-processing`.
Build: Debug, `-DENABLE_TESTS=ON` + `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`,
`-j2` cap. Tests need `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`.

## 缺陷→oracle 对应表(全部先在 master 复核代码路径,RED 由旧实现语义推得并在本分支首个运行中确认)

| # | 缺陷 | Oracle(test 文件 / case) | RED on master(旧行为) | Fix |
|---|---|---|---|---|
| F1 [P1] | `positionalFallbackBand` swir2/red_edge 钳制回退,4 波段栈 NBR ≡ 全 0 且退出成功 | test_rs_operators "Spectral index refuses degenerate positional duplicate bands" | 输出全 0 栅格、exit success,仅一条泛化 warning | 参与角色重复带 + 至少一侧为解析器回退 → InvalidParameter("degenerate all-constant ratio") |
| F2 [P1] | apply_mask 单侧缺 geotransform 时 compareGrids 报 compatible → 位置对齐静默应用 | test_rs_operators "apply_mask requires the dimension contract …" | 8×8 无 GT mask 套 4×4 输入:成功写入(取 mask 左上窗) | 任一侧缺 GT → 维度契约(不等即拒)+ 单侧时 info 日志 |
| F3 [P2] | facade alias 认可未声明的 `index` 参数:rs:ndvi 可写 MNDWI 产物 | test_rs_operators "alias operators refuse a conflicting index override" | rs:ndvi + index=MNDWI 成功产出 MNDWI 栅格 | alias 拒绝非恒等 `index`(generic 算子 allowIndexOverride=true) |
| F4 [P2] | extract_bands 丢弃 NoData/role/wavelength/辐射态/numeric scale 声明 | test_rs_operators "extract_bands carries NoData, band roles and provenance stamps" | 输出无任何声明:后续 NDVI 把 −9999 当数据(≈−1 patch)、#680 域解析失真 | 逐波段 + 数据集级传播(GdalStreamingOutput 新增 setBandMetadataItem) |
| F5 [P2] | recode 对 float label 直接 `static_cast<int>`:哨兵/NaN → UB(INT_MIN 伪类) | test_rs_operators "recode refuses non-integer label values …" | −3.4e38 像素 → 输出 −2147483648 类,exit success | 非有限/超 int 域 → InvalidInputData,输出不落盘 |
| F6 [P2] | majority filter 上界 `fv <= float(INT_MAX)`:`float(INT_MAX)=2^31` 恰被放行 → UB | test_rs_operators "majority filter treats the 2^31 float boundary as non-voting" | 2^31f 像素 cast 为 INT_MIN 并参与投票 | 上界改为排他 `< 2^31f` |
| F7 [P2] | feature_stack 接受 scale=0/NaN:整波段被替换成 0/NaN 且契约声称 scale=1 | test_rs_operators "feature_stack validates scale/offset …" | scale=0 → 全 0 波段,exit success | scale 有限非零、offset 有限,否则 InvalidParameter |
| F8 [P2] | feature_stack 用 WKT 字符串相等比较 CRS:同 CRS 不同编码被拒 | `…compares CRS semantically, not by WKT encoding…`(**正向控制**,非 RED:本 GDAL 构建的 `GDALGetProjectionRef` 会把两种编码统一导出为相同 WKT1,详见测试内注释与 review P2-1) | 同 CRS 不同 WKT 编码 → blocking 拒绝(代码路径级确认;本平台 GDAL 导出归一化使该失败模式弱化) | `sicnu::data::isSameCrs`(OSRIsSame) |
| F9 [P2] | PartialOutputGuard 逆序析构先删后关 GDAL 句柄:Windows 共享违规 → fail-open | test_rs_operators "PartialOutputGuard closes registered datasets before removing paths" | 旧 API 无 close-first 钩子(语义不可表达) | `setCloseFirst()` + 4 个调用点注册 |
| F10 [P2] | band_math 求值无取消探针:大影像取消延迟无上界 | test_rs_operators "BandMath::processFile honours the cancellation probe" | 旧签名无探针;中途取消只能在头尾检查 | 每块探针 + 取消时 "Cancelled" 且不落部分输出 |
| F0 [P2] | in-memory `ihsFusion` NaN 输入 → `std::max(0,NaN)` 黑像素(与流式路径/兄弟内核不一致) | test_image_fusion "IHS: NaN NoData pixels stay NoData instead of collapsing to black" | NaN 孔洞 → (0,0,0) 合法黑像 | 三处循环补 isnan 门(哨兵 + NaN 双约定断言) |

## 运行记录

**PR: #1273**(创建于 rebase 至 master `abc07b715` 之后;按 campaign 规则不 merge、不等待线上 CI)

构建:Debug, ninja -j2, `-DENABLE_TESTS=ON -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`,
测试以 `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib` 直接运行(ctest 注册名单集大,
direct binary 运行等价)。

- [x] 第一遍:`test_rs_operators [hardening16]` → 10 cases / 93 assertions 全过
- [x] 第一遍:`test_rs_operators` 全量 → 79 cases / 9041 assertions 全过
- [x] 第一遍:`test_image_fusion` 全量 → 30 cases / 1417 assertions 全过
- [x] 第二遍(连续两遍):`[hardening16]` 10 cases 93 assertions + `test_image_fusion`
      30 cases 1417 assertions 再次全过
- [x] 相邻 suite 回归面(9 个,全绿):
      test_band_math(49 cases/130277 assert)、test_rs_band_tools_operators(6/132)、
      test_radiometric_calibration(33/195712)、test_atmospheric(40/37486)、
      test_radiometric_qa(8/75)、test_obia_operators(16/145)、
      test_spectral_index_asset_pipeline(5/50)、test_d13_radiometric_spectral_e2e(4/67)、
      test_feature_cube(5/41)
- [x] 改动 TU 无新增 warning(16e/16f 构建日志核对:band_math_simd 的 omp pragma、
      image_fusion 的 dot/normSq/tileSize、rs_mnf_inverse 的 totalTiles 均为 master 预存
      warning,行号在 master 上逐一确认)
- [x] 独立 adversarial review:**READY,无 P0/P1**。reviewer 以"换入 master 实现文件重建"
      实证 RED:master 语义下 `[hardening16]` 10 例中 7 例失败(NBR/alias/fs-scale/apply_mask/
      recode/majority/extract_bands),`test_image_fusion` 新 NaN case 2 处失败;两处新 API
      测试(band_math cancel、guard)与 CRS case 在 master 上通过(前者为新能力无旧对应,
      后者见 F8 行的诚实降级)。分支恢复后全部复绿。
      Review 发现并已修复:P2-1(CRS 测试注释失实 → 改为正向控制并同步 ledger)、
      P3-1(guard 消息措辞 side-neutral)、P3-2(apply_mask 错误措辞 side-neutral)、
      P3-3(recode 去除每像素双次守卫调用)、P3-4(recode 错误文案补 float32-exact 说明)。
      Reviewer 逐项核清:guard-before-output 顺序、setCloseFirst 无 double-close/UAF、
      cancel 部分输出清除、显式-显式豁免、空 CRS 行为不变。

## 已知限制(P3,不在本轮关闭)

- F11 apply_mask `floor` vs `round` 半像素偏差(mask 更细时);
- F12 threshold_raster percentile/cleanupIterations 未验证;
- F13 未声明 ±inf 在 change-mask 计为 changed;
- F14 recode schema 字符串 vs 运行时对象双形态;
- F15 contrast_stretch clipPercent/stddevK 未验证;
- F16 getInt 截断小数(框架级,影响面大,需单独评估);
- F17 apply_mask #612 注释对 Int64 不成立;
- F18 spectral_index_operator 死前向声明;
- quality report 失败时融合输出保留但 operator 报错(语义不一致,建议下轮)。
