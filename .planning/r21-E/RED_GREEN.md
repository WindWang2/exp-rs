# R21-E RED_GREEN — 驱动证据（每项修复 RED→GREEN 实录）

## W2 faultlab verifier（#1613）
- **RED 驱动**：`tests/test_r21_e_faultlab_verifier.cpp` case 1（loader accepts schema-documented verifier）
- **RED 执行**（BASE=911e0b17，生产码 stash 后）：
  ```
  tests/test_r21_e_faultlab_verifier.cpp:104: FAILED: REQUIRE( result.ok ) with expansion: false
  test cases: 3 | 2 passed | 1 failed
  ```
  BASE loader 拒绝 faults.schema.json:160-171 自己文档化的 `expected.verifier`（"must be empty"）。
- **GREEN 执行**（faultlab 核+loader+runner 折叠恢复后）：`All tests passed (8 assertions in 3 test cases)`（首版）；扩展 runner 语义 3 组 case 后待重跑（构建排队）。
- 修改：fault_types.{h,cpp}（FaultVerifierSpec+严格 parse）、fault_scenario.cpp（接受+校验）、fault_runner.{h,cpp}（hook 类型+第 7 合取）、fault_report.{h,cpp}（条件序列化）、studio_live.cpp+其 CMakeLists（生产钩子安装：FaultGrid→RasterWriter 物化→gradeForTeaching→must_fail⊆failed）。

## W1 verifier 生产接线（#1601）
- **RED 驱动**：`tests/test_r21_e_verifier_evidence.cpp`（4 case：graded 携带证据/indeterminate 非 pass/usage 无证据/byte 确定性）
- **RED 原理**：master 的 LabGradeResult 无 verifier 字段——`body.isMember("verifier")` 恒 false；RED 构建进行中（qgis_core 首编，-j4）。
- **GREEN 修改**：`src/verify_adapters/lab_verify_composer.{h,cpp}`（默认教学 spec exists/type/grid(pins)，fs+gdal probe，三值投影+封印）；agent `output_verifier.{h,cpp}` 6 挂载点+条件序列化；`src/agent/CMakeLists.txt` link `sicnu_verify_adapters_gdal`；`src/verify_adapters/CMakeLists.txt` 编入 composer。
- **回归风险已排除**：无存档 digest fixture（test_lab_grading 全部同跑比对）；gradeFromTranscript 按字段读、宽容未知 member。

## W3 help/explain（#1614/#1615/#1616/#1639/#1636）
- **无需修复**：五项全部在 master 911e0b17 实读确认已修（详见 ISSUE_TRIAGE 复测列，行号+注释引用 issue 号）。不制造无效 commit（E0 纪律）。

## W5 批量压力（O-E2）
- 驱动：`tests/test_r21_e_batch_stress.cpp`（100/500/1000；记账不变式：rows==total、total==graded+isolated+unavailable、duplicates⊆graded、重跑 byte 等同=中断治愈、耗时/RSS INFO 记录不作门）。
- 现状核对：CSV Truncate 打开（无追加叠加）；cancelled 探针支持中断；determinism 已有 24 份级测试。
===============================================================================
All tests passed (8 assertions in 3 test cases)


## W1 补充 RED 证据（编译期，2026-10-24 08:3xZ）
- BASE 头文件（W1 stash 中）上引用新成员：
  `error: 'struct sicnu::agent::OutputVerifier::LabGradeResult' has no member named 'verifier'`
- 行为级 RED（body.isMember("verifier")==false）由 redgreen_chain 后续产出（qgis 链接依赖构建锁）。
