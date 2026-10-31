# PERF BASELINE
本轨为 fail-closed 语义修复（证据分类 + 事实门），无热点路径改动；性能报告以**等价输入对照**方式记录：
- evidence 路径：每个 output 新增 1 次 sidecar 读（inspect 替代 read，I/O 次数不变）+ Corrupt 分支 2 次 exists 探测（仅损坏时）。
- run_loop diagnose：每 output 新增 1 次 inspect 读（与既有 read 同次数，非叠加）。
- planner：两门各一次 O(assets) 扫描 + 一次 contracts 查表（buildCandidatePlan 已有 ≥3 次同类扫描）；可忽略。
- familyMap：std::map 静态初始化，查询 O(log n) 不变。
测量（Debug，-j4，flock）：test_r26_harness_evidence_integrity 71 断言 8 用例全程 < 2s（真实 GDAL 8x8 raster sidecar 往返），test_r26_capability_family_drift 遍历 158 sidecar < 1s。与修复前同输入同资源对照无可测量差异（无 wall-clock 门禁，共享宿主）。
