# hardening(r19-01): data fabric, model runtime and reproducible persistence

## 基线与 HEAD
- BASE_SHA: `e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（origin/master 2026-10-19T12:35Z；PR 创建前复核未移动）
- HEAD_SHA: 92e00385594a45730fc176f5830f3b1a56adaef2
- 分支: `agent/r19-01-data-model-runtime-20261020T102418Z`

## LIVE recon 结论（本 PR 的第一交付物：盘点价值）
对 swarm-review 遗留的 22 个 P1/P2 issue 逐项做了**代码级**核实（OWNERSHIP.md 映射表全量证据）：
- 21 项在当前 master **已修**但 issue 仍 Open（#1545/#1546/#1553/#1554/#1579–#1587/#1595/#1597/#1603–#1605/#1467-P1 等）——本 PR 不重复修复，仅钉住/引用既有测试
- **O-A1 客观纠偏**：#1662 报告"仍红"的两条预存测试在当前 master 基线即绿（#1548 resolver 2 断言 / #1456 cols 13 断言，POSIX lane；退出码留档 TEST_RESULTS.md）
- #1576 为已文档化设计决策；#1635 已过时（代码已重构）——均不建议改动

## 修复清单（全部 RED→fix→GREEN×2，证据见 ORACLES.md / TEST_RESULTS.md）
| commit | 内容 | RED | GREEN |
|---|---|---|---|
| 40674def9 | format_profiles `profileForPath` 最长点锚定后缀匹配——`*.zarr.json` 此前结构上永不可达（GeoJSON 遮蔽），doctor/probe/capabilities 全部误报（#1637-2）| exit 42 ×3 断言 | 10/10 ×2；兼容钉（bare .json→GeoJSON、.tif→GeoTIFF、.tar.gz→nullptr）|
| d040e9325 + bfbd8d768 | setRange 数值区间运行时强制（#1578）：72 处声明此前纯元数据。校验落 `sicnu_operators_core` 叶库（rs_param_validation，std+jsoncpp 预算内，#573 ODR 防护保持），接入 execute() **与两个生产漏斗**（atomic_algorithm_adapter / job_engine——R1 评审发现原实现只挂 execute() 而生产全走 run()）| exit 42（radius=0 静默接受）| 6/6 ×2；全体注册 operator 831 断言在新门下零回退 |
| 17b24d394 + bfbd8d768 | CN 产品元数据 4MiB 有界读（#1637-3 孪生）：`buffer << rdbuf()` 无界整读 → 分块读、超限即类型化 InvalidMetadata 拒绝（R1 评审后升级：FIFO/stat-TOCTOU 不可绕过）| exit 42（5MiB 被整读解析）| 6/6 ×2 |
| 501bcaedd | 域 sweep 发现的预存红：test_chunked_fabric_e2e 两断言期望漂移（Action::Advisory 枚举 + F-A-13 in-hand 记账下的 10MiB 预算）| sweep 606,363/606,365 | 606,370/606,370 |

## 测试
- 域全量 sweep：85 目标 → **83 PASS**；1 NOT_EXECUTED（test_model_runtime_bench：头文件明示 env-gated 设计性跳过，不谎报）；1 预存红已修复（上行）
- O-A2 负向矩阵（SSRF/offline/identity：remote_validator 232 + range_cache 314 + remote_range 25 + uri 211 + stac 27 断言）**两遍全绿**
- 首轮 22 个 sweep 失败全数归因为直跑二进制缺 PROJ_DATA（环境），补齐后 21 立即全绿——留档说明，非代码回归
- 构建环境排障记录（vendor 静态/共享库钉库配方、rpath-link 顺序、无 ctest 宿主的替代驱动）固化于 configure.sh / run_tests.sh / TEST_RESULTS.md

## 性能
N/A：改动均为元数据解析/参数校验/有界读路径，不触及像素循环（PR 声明，无基准需求）

## 评审
- R1（独立子代理，逐文件逆向）：REQUEST-CHANGES → P1（生产路径不可达）+ 2×P2 + 4×P3 全部闭环于 bfbd8d768（REVIEW_R1.md）
- R2（独立子代理，增量+跨轨终审）：REVIEW_R2.md（附）

## 跨轨
- 与开放 PR #1663/#1664/**#1665/#1666** 文件级零重叠（含新开的两条 r19 平行轨，已实测 diff 比对）
- 修改的三个共享但非红线文件（src/operators/CMakeLists.txt 叶库源列表 append、atomic_algorithm_adapter.cpp、job_engine.cpp）均为最小增量
- CROSS_TRACK_FINDINGS.md：#1588/#1589/#1590/#1467-余项/#1656 移交对应轨道；X1 残余直调 run() 的入口（pybind/batch dialog/worker mains）属他轨写集

## 已知风险与遗留
- git push 通道：https 直连曾超时（gh API 正常）——push 失败将重试并留档
- STILL_OPEN：无本轨 P0/P1 未闭环；KNOWN_BASELINE_FAIL：无（预存红已修）；NOT_EXECUTED：model_runtime_bench（设计性 env-gate）、TSan 未跑（未标 PASS）
