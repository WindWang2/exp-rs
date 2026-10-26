# R23-B ISSUE_TRIAGE — 144 open issues vs B-scope（基线 ad98a7a704，2026-10-27）

方法：3 个独立只读审计代理逐条读 issue 正文+当前代码+pickaxe 修复提交；证据存 triage_geo_data.md / triage_dataset_experiment.md / triage_runtime_operators.md。Open≠未修：R20-R22 已合并修复大量留在 open 状态。

## B 域相关（逐条）

| Issue | 判定 | 证据（file:line / commit） |
|---|---|---|
| #1467 | OUT_OF_SCOPE-B | src/core-fork 簇（temporal_cube/radiometric_state 等），非 B 白名单 |
| #1491 | UNVERIFIABLE(线上CI) | 本机同 GDAL 运行 test_io_vector_contract 判定（见 TEST_RESULTS）；线上 CI 无完成 run 不可复现 |
| #1541 | FIXED_OPEN | CV_CN_MAX 护栏 9b42bc93f7 detection_tile_engine.cpp:331 |
| #1545 | FIXED_OPEN | SSRF 标志传播 987161c255 range_cache.cpp:292+三探针 |
| #1546 | FIXED_OPEN | 遮蔽变量删除 831f8d6006 repeat_execution.cpp:201+ |
| #1547 | FIXED_OPEN | identityDiffers 831f8d6006 replay_deviation.cpp:159 |
| #1548 | FIXED_OPEN | 锁域拆分 8e04302629 execution_identity_bridge.cpp:158 |
| #1553 | FIXED_OPEN | requireFreeConsumerLocked d9f1b886f1 chunk_graph.cpp:58 |
| #1554 | FIXED_OPEN | 析构持锁 d9f1b886f1 gpu_plane.cpp:44-64 |
| #1558 | OUT_OF_SCOPE-B | tests/test_e2e_open_issues.cpp 恒真断言——共享测试文件、断言域属 recipes/tasks（他轨）；记录不扩 |
| #1575 | FIXED_OPEN | halo+input 尺寸计入 9b42bc93f7 tile_inference_engine.cpp:4279 |
| #1576 | PARTIAL→本轨W1 | selectDevice 定义被合并 9988aa15a9 丢弃，OV TU 编译断；本轨恢复 |
| #1577 | PARTIAL→本轨W1 | TRT artifactPath() 返回未声明 m_artifactPath；本轨修复 |
| #1578 | FIXED_OPEN/OUT_OF_SCOPE-B | boundedInt/Double 在位但属 src/operators/otb |
| #1579 | FIXED_OPEN | percentDecode 987161c255 resource_uri.cpp:973 |
| #1580 | FIXED_OPEN | RunId 门 831f8d6006 reproduction_bundle_import.cpp:352 |
| #1581 | FIXED_OPEN | 显式孤岛告警 831f8d6006 :493-501 |
| #1582 | FIXED_OPEN | 逐因跳过计数 831f8d6006 benchmark_runner.cpp:446-463 |
| #1583 | FIXED_OPEN | foldTotal 派生 c8ba519901 fold_audit.cpp:88-95 |
| #1584 | FIXED_OPEN | 双侧窗门 c8ba519901 leakage_audit.cpp:608 |
| #1585 | FIXED_OPEN | 对称对去重 c8ba519901 :440-442 |
| #1586 | FIXED_OPEN | BEGIN IMMEDIATE 单事务 c8ba519901 dataset_store_splits.cpp:254-309 |
| #1587 | FIXED_OPEN | 四写全检 8e04302629 workspace_service.cpp:222-246 |
| #1593 | FIXED_OPEN/OUT_OF_SCOPE-B | lab_pack.cpp:249 已修，属 src/lab_pack |
| #1603 | FIXED_OPEN | 30s 时限+maxPolls d9f1b886f1 execution_governor.cpp:46 |
| #1604 | FIXED_OPEN | thread_local ofstream d9f1b886f1 trace.cpp:269 |
| #1605 | FIXED_OPEN | 环形覆写 d9f1b886f1 execution_telemetry.cpp:111-114 |
| #1635 | FIXED_OPEN | buildCells 合并 831f8d6006 evaluation.cpp:394-449 |
| #1637 | PARTIAL(sub-4) | sub1-3 FIXED；sub-4 probeRemote 重定向盲区=本轨W4 |
| #1638 | FIXED_OPEN | 1 项已修（dataset domain report 核对） |
| #1642 | FIXED_OPEN/混合域 | 4 sub 全修；sub2 opencv_dnn 在 B 域，其余他域 |

## 其余 ~118 条：OUT_OF_SCOPE-B

P3 技术债汇总（#1625-#1658 等，按目录归属 A/C/D/E 轨）、R17-agent/#1690-#1705（agent 轨，#1714 已修其主体）、R17-infra/#1681-#1699（infra 轨）、R17-ui/#1678/#1688/#1689/#1694/#1695（ui 轨）、#1595-#1618（cli/python/faultlab/verification/help/explain 等非白名单目录）、#1549-#1572（analysis/preflight/agent/processing/app）、#1620-#1624（build/i18n/性能门槛）、#1682 CI 平台覆盖、#1683 脚本卫生。逐条目录-白名单比对完成，无 B 域遗漏；其中 B 相邻但明确他域的已在上表标注。


## 结论

- B 域 LIVE 仅 3.5 项：#1576（半）、#1577（半）→ W1；B5 残留 → W2；A5 残留 → W3；#1637 sub-4 → W4。
- 其余 B 域 issue 全部 FIXED_OPEN（真实修复在位，建议维护者按证据关闭；本轨不擅自批量关 issue）。