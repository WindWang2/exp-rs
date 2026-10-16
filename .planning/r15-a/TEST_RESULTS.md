# R15-A TEST_RESULTS — 测试证据台账

> 环境：Windows 11 + MSVC 14.38.33130 + Qt 6.8.0 msvc2022_64 + vcpkg classic（GDAL/PROJ/OpenCV），隔离构建树 `build-r15-a`，`-j2` 编译 / `-j1` 测试。
> 运行器：`run_r15a_test.ps1`（applocal DLL + Qt bin + qca/kc bin + vcpkg debug/release bin；`QT_QPA_PLATFORM=offscreen`；`PROJ_LIB=<build>/vcpkg_installed/x64-windows/share/proj`）。

## 最终状态（HEAD = 362dc61af，评审响应后）

| 套件 | 用例 | 断言 | 结果 | 备注 |
|---|---|---|---|---|
| test_image_fusion | 36 | 1615 | ✅ 全绿 | 含 O1 三锁（重写后）、A14 两例、[r14a] 修正 fixture |
| test_spectral_classification | 30 | 197 | ✅ 全绿 | 含 O2 双锁 |
| test_obia_segmentation | 14 | 850 | ✅ 全绿 | 含 A09 RED→GREEN 锁（9187cdc79） |

每套件均按 Oracle-Ledger 纪律执行 **GREEN×2**（连续两轮全绿）后才进入记账。

## RED→GREEN 链条

| 工作包 | RED 证据 | 修复 | GREEN 证据 |
|---|---|---|---|
| A09 | 9187cdc79：2×2 buffer `{5e-7,5e-7,5e-7,0.0}` nodata=0.0 → epsilon 谓词吞合法值 → 空 map（RED） | 28a1445c5：`sicnu::rs::isNoDataValue` 精确比较 | test_obia_segmentation 14/850 ×2 |
| A03-6（别名 A14） | 新增测试先行：pan GT+CRS、MS VRT 有 SRS 无 GT → BASE 融合"成功"且 12/16 像素 void 无报错（RED） | bf5a7d27a：hasGeoTransform 对称性阻塞 | test_image_fusion 36 用例全绿 ×2 |
| O1 | 原 RED（7aae63038/b4a7d148b）经 diag1–diag4 实证为 fixture 缺陷 → 重定性 FALSE POSITIVE，测试重写为 GREEN 回归锁 | 无生产改动（行为已正确） | 6d5672419 后全绿 |
| O2 | 双锁测试（BASE 即绿，无行为改变） | 无（决策：钉死不统一） | 96b85fef4 后 30/197 ×2 |

## 关键实证数据（diag 链，O1 重定性依据）

| 实验 | 输入 | 输出 |
|---|---|---|
| diag1/diag2 | VRT（band2 声明 NoData=-1）双线性上采样 2×2→4×4 | `{-1, 2×15}`：空值 tap 被排除、权重重归一化；空值仅在主导 tap 处透传 |
| diag3 | 无 GT 的 VRT + 流式 brovey | `{NaN, 6.667, NaN, NaN, 6.667, 6.667, NaN×10}` = 同分辨率钳位路径产物 |
| diag4 | 无 GT VRT（`hasGT=no gt=[0,1,0,0,0,1]` 默认单位变换）vs 带 GT VRT（`hasGT=yes gt=[0,2,0,0,0,-2]`） | 无 GT：`{NaN,6.667,NaN,NaN,6.667,6.667,NaN×10}`；带 GT：`{NaN,6.667×15}/{NaN,3.333×15}` ← 正确家族行为 |

## 环境修正记录

- vcpkg debug proj.dll 编译期内嵌数据目录（debug/share/proj）不存在 → 直接运行 exe 时设 `PROJ_LIB` 指向 release share/proj；缺失时 CRS 比较静默失效，"拒绝非同配准"测试假失败。已固化进 run_r15a_test.ps1。
- Launch-VsDevShell.ps1 不可用（reg.exe 被安全策略拦截）→ build_r15a_noshell.ps1 手工设置 MSVC/SDK 环境。

## 构建证据

| 项 | 值 |
|---|---|
| 构建树 | build-r15-a（本轨专用隔离树，configure exit 0） |
| 增量构建 | test_image_fusion / test_spectral_classification / test_obia_segmentation 均 exit 0 |
| 全量重编触发 | A09 修改 rs_multires_segmenter.cpp → sicnu_analysis 依赖闭包 517 edges，17m13s，exit 0 |
