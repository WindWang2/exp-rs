# R19-01 CROSS_TRACK_FINDINGS — 跨轨发现（只记录，本轨不改）

## 提交其他轨认领
| # | 发现 | 建议认领轨 | 状态核实（本轨基线 e12a9afbc）|
|---|---|---|---|
| C1 | #1467-[P1] src/core/temporal_cube.cpp 场景读失败静默 NaN | core-fork 维护轨 | **已修**（mSceneReadFailures 计数 + qWarning，:399-425）|
| C2 | #1467-[P1] src/core/radiometric_state.cpp:101 preflight GDALOpenEx 原样打开 provider URI | core-fork | 未核实（非本轨域）|
| C3 | #1467-[P2×4] spatial_split/temporal_cube 归属、QWidget 头、int64 溢出、qgsdatasourceresolver file:// | core-fork | 未核实 |
| C4 | #1588 src/preflight temporal 截断无计数 | 数据轨/preflight 维护者 | 未核实 |
| C5 | #1589 src/science_context invalidateAsset 链未接线 | 科学上下文轨 | 未核实 |
| C6 | #1590 src/analysis sieve NoData 票数 | R18-A 邻域（#1663 合并后处理）| 未核实 |
| C7 | #1656 tests 等待助手 17 文件重复 | 测试基建轨 | 确认未修（P3；涉及公共 tests/CMakeLists.txt 临界区）|

## 行为性跨轨风险（本轨 PR 合并前需知）
| # | 风险 | 涉及 | 缓解 |
|---|---|---|---|
| X1 | T1.2 setRange 运行时强制改变**所有** operator 家族的行为面（含 R18-A #1663 的算法文件在运行时的参数校验）| src/operators/framework/rs_operator.cpp | 仅对显式声明 range 的参数生效；#1663 测试若传越界值即属缺陷；合并前用 #1663 分支交叉编译验证（若可达）|
| X2 | tests/CMakeLists.txt 与 #1664 冲突 | 公共文件 | 本轨零 CMake 改动（新测试全部落在已注册文件内）；PR 说明中声明 |
