# R15-A LEDGER — Oracle-Ledger 循环记账

> 循环：分析 → RED → 最小修复 → GREEN×2 → 记账 → 下一任务。连续三轮无进展须改变假设；八轮无进展回查前提。每阶段可恢复 checkpoint。

| # | 任务 | Oracle | 状态 | 证据 |
|---|---|---|---|---|
| L0 | M0 事实冻结（BASELINE/OWNERSHIP/矩阵/ORACLES） | — | DONE | origin/master=46791bc6；0 open PR；#1505/#1509/#1512 已合并；25 issues 分类完成 |
| L1 | 构建树 build-r15-a 配置 + 9 个优先目标编译通过 | — | DONE | configure exit 0（VCPKG_MANIFEST_MODE=OFF + VCPKG_INSTALLED_DIR + OpenCV_DIR 修正）；test_image_fusion/test_obia_segmentation 等目标均构建+运行成功；运行器 run_r15a_test.ps1 就绪（含 PROJ_LIB 修正） |
| L2 | O3：#1465 3-arg std::equal 定位与分类 | O3 | DONE（静态清扫） | 全仓 8 处：asset_state_json 4 处已有长度门卫（FIXED_ON_MASTER）；temporal_change.cpp:218 尺寸相等前置；study_spatial.cpp:169 定长 6 栈数组；qgsabstractgeometry（QGIS 只读）。结论：本分支无存活缺陷 |
| L3 | O5：rs_spectral_index #1509 diff 审计 + 回归基线 | O5 | DONE（静态） | 共享 JSON 辅助（rs_json_params.cpp getInt/getString/getDouble/getBool）全部 isNumeric/isString/isBool 门卫 + #647 int64 范围检查；rs_spectral_index_operator 全部经辅助路由，scale 参数 isNumeric+finite>0 双检；锁已随 #1509 合并解除，无残留缺陷 |
| L4 | O1：融合 resample-then-mask 边缘掺混 RED→GREEN×2 | O1 | DONE（**FALSE POSITIVE**） | diag1–diag4 实证：RED 模式系 fixture 缺陷（VRT 缺 `<GeoTransform>` → GDAL 默认单位变换 → 同分辨率钳位路径），非 GDAL 掺混。带 GT+SRS 的正确 fixture 下 BASE 输出 `{NaN,6.667×15}`/`{NaN,3.333×15}`（void 正确 void、邻域重归一化干净值）——与家族惯例一致。原 RED 测试已重写为 GREEN 回归锁（6d5672419）；附带暴露 A14（单侧无 GT 静默垃圾产出）已修复（bf5a7d27a） |
| L5 | O2：sam/spectral_similarity NoData 方言双锁或统一 | O2 | TEST_READY | 双锁测试已写入 tests/test_spectral_classification.cpp（未提交）；sam metadata 文档已改（未提交）；待构建验证 |
| L6 | O4：#1454 科学算子回调侧审计 | O4 | PENDING | — |
| L7 | A03-6（提交信息记作 A14，**新发现**，勿与工作包 A14=性能混淆）：单侧缺 GeoTransform 的非对称配对（O1 调查新发现） | A03-6 | DONE | RED：pan 有 GT+CRS、MS VRT 有 SRS 无 GT → BASE 融合"成功"且 12/16 像素 void 无报错。修复：processNativeFusion 增加 hasGeoTransform 对称性阻塞（bf5a7d27a）；both-unreferenced 合成回退保留并有测试锁定；[r14a] fixture 补 GT。36 用例/1611 断言 GREEN×2。驱动修复：%.6g→QString::arg 日志告警 |
| L8+ | A01–A16 工作包审计新发现（按复现准入排队） | O6+ | IN_PROGRESS | 见 FINDINGS.md；已完成 A02/A03/A13 静态审计；A09 修复已应用（待 GREEN 验证） |

## 阻塞与假设变更记录

- 假设变更（重大）：O1 的"GDAL 双线性跨哨兵掺混"前提被 diag1–diag4 实证推翻——本环境 GDAL 的 GRIORA_Bilinear 对声明 NoData 的波段是 NoData 感知的（排除空值 tap + 权重重归一化），与 resampler.cpp 家族惯例一致。O1 原 RED 模式实为测试 fixture 缺 `<GeoTransform>` 导致的同分辨率钳位路径产物。O1 关闭为 FALSE POSITIVE。
- 假设变更：vcpkg manifest 模式在本环境不可用（reg.exe 被拦 → SDK 探测失败 → 编译器哈希不匹配），切 classic 模式 + 复制主仓 vcpkg_installed 树；configure 两次误删复制树后已恢复。
- 假设变更：不跨树复制 build-dev 的 6430 个预编译 .obj（保 M4 隔离构建树证据链纯净）。
- 环境修正：直接运行测试 exe 时需设 PROJ_LIB=<build>/vcpkg_installed/x64-windows/share/proj（vcpkg debug proj.dll 编译期内嵌数据目录 debug/share/proj 不存在；缺它时 CRS 比较静默失效，"拒绝非同配准"测试假失败）。已写入 run_r15a_test.ps1。

## Checkpoint 索引

- CP0（本轮）：M0 完成，分支 `hardening/r15-A-science-processing` @ 46791bc6 + M0 文档提交。
- CP1：configure exit 0；O1 RED 测试提交（流式 + 质量报告两路径）；O2 双锁测试就绪；A02/A03/A13 静态审计完成（详见 FINDINGS.md）。
- CP2：O1 关闭为 FALSE POSITIVE（6d5672419，diag1–diag4 证据链）；A14 非对称地理参照阻塞修复（bf5a7d27a，36 用例/1611 断言 GREEN×2）；%.6g 日志缺陷驱动修复。
- 下一 session 恢复点：读本文件 + FINDINGS.md + BASELINE.md；A09 修复已应用待 test_obia_segmentation GREEN×2；O2 待 test_spectral_classification 构建验证后提交。
