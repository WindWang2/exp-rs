# PLAN — core-foundations-r4

## Phase 0(完成于 BASELINE.md)
worktree 隔离;锚定复核;overlap map;四份 dossier 缺失降级;build-r4 配置成功。

## 构建策略(资源红线内)
- 全程 `-j2`、`CTEST_PARALLEL_LEVEL=1`;`QT_QPA_PLATFORM=offscreen`。
- **门禁口径(如实记录)**:Oracle 正则 `core|atomic|portab|utf8|fs|inject` 按二进制名命中 11 个目标:
  轻量:test_io_atomic_failures、test_portability_contract、test_platform_portability、test_portability_source_contract + 本轨道新增(io 级);
  重型:test_atomic_algorithm_adapter、test_atomic_algorithm_registry、test_atomic_registry_contract、test_dataset_core、test_fault_injection、test_store_fault_injection、test_harness_lab_injection(全栈链接 qgis_core/qgis_gui/sicnu_*)。
- 11 目标全量闭包构建于 Phase 1 起后台进行(-j2),与读审计工作重叠;基线红绿分布与最终双跑都以此为口径;tests/ 下其余 237 个目标不在正则内,不构建、不运行、不声称。

## WP 执行序(纵向切片,TDD)
- WP-A(portability):以 `test_platform_portability` / `test_portability_source_contract`(纯 Catch2)为载体;残留处置 = 1×CreateFileW(qgsfileutils.cpp:340,QGIS vendor)+ 12 文件×GetLastError(QGIS vendor)→ 处置口径见 DECISIONS.md D-2;产出 PORTABILITY_COVERAGE_REPORT.md(11 helper × 模块矩阵)。
- WP-B(审计):35 行矩阵,分 5 批(每批 6-7 行)推进;偏差修复仅落非避让文件;合同测试 `tests/test_atomic_fs_caller_contract.cpp`(io 轻量注册)按合同语义书写。
- WP-C(注入):`tests/test_core_failure_injection_r4.cpp`(io 级起手,合同面 9 + portable 4 + 调用方类按闭包可用性推进);参照既有 test_fault_injection.cpp 模式。
- WP-D(WP-E/WP-F/WP-G)依 Phase 2-4 展开。

## 退出条件(Oracle §六)逐条映射见 EVIDENCE.md 骨架。
