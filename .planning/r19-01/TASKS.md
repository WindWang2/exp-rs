# R19-01 TASKS — 按优先级

## P0（Gate：完成前不得进入扩展轮）
- [ ] T0.1 构建基线（Release, Ninja, 全局 -j2 锁）→ TEST_RESULTS.md 记录编译器/依赖/时长
- [ ] T0.2 **O-A1 复现** `test_io_remote_validator` 全量 + `test_io_fabric_multidim_11` 全量：
  记录两种子测试真实状态（红→定位根因 / 绿→保存证据并纠偏"预存失败"定性）
- [ ] T0.3 本轨域测试基线：`test_io_* test_chunk_* test_model_* test_dataset_* test_experiment_*`
  全量跑一遍，KNOWN_BASELINE_FAIL 清单落盘
- [ ] T0.4 已修 issues 的测试钉住（防回退）：#1553/#1554/#1603/#1605/#1546/#1545 若无现成测试则补
  （先查已有测试再决定，禁止伪绿）

## P1（真实缺陷修复，RED→FIX→GREEN×2）
- [ ] T1.1 **#1637-2** format_profiles `profileForPath`：Zarr `zarr.json` 两段式扩展名与 STAC `json`
  声明永不可达（GeoJSON 先注册先命中）。RED：`tests/test_io_format_profiles.cpp`（或既有注册测试）加
  `store.zarr.json`→zarr、`item.json`(STAC 特征)→stac 用例
- [ ] T1.2 **#1578** setRange 数值区间运行时强制（framework 最小强制点；先审 rs_json_params/rs_schema
  结构与 72 处调用面，选一个不破坏现有合法调用的接入点；仅对显式声明了 range 的参数生效）
- [ ] T1.3 O-A1 复现出的任何真实失败的最小修复（等 T0.2 结果）
- [ ] T1.4 **O-A2** SSRF/offline/identity 负向矩阵：A03 工作包的确定性本地假 HTTP server 用例
  （DNS 轮换/redirect/私网/端口/206/200/416/ETag/generation invalidation/detach shutdown）
  —— 先盘点 tests/test_io_remote_range.cpp 已覆盖面，只补缺口
- [ ] T1.5 **O-A5** dataset/experiment crash-safe：A04+A05 工作包（sidecar 混代、损坏 bundle 拒绝、
  半发布回滚、SQL 事务并发）—— 先盘点现有测试，补可复现缺口

## P2（条件触发：P0/P1 完成后）
- [ ] T2.1 #1635 evaluation.cpp boundaryFScore 重复代码合并（P3 重构，行为不变 + 金样对照）
- [ ] T2.2 A06 ChunkGraph/ChunkPipeline 背压/取消/析构顺序探针（盘点 chunk 测试矩阵缺口）
- [ ] T2.3 A07 模型运行时：tile overlap/feather/NaN/NoData 语义探针；GPU OOM 降级 CPU 契约（无 GPU 时
  标 NOT_EXECUTED，用 fake provider 覆盖逻辑）
- [ ] T2.4 A09 性能剖析：per-tile allocator/锁热点（仅在 profile 证实后动；A/B 同参对照）
- [ ] T2.5 R18-B 未测矩阵（发布 sidecar、provider degrade、cancel/VRAM）—— 读 #1659 diff 确认缺口

## P3（债务）
- [ ] T3.1 #1656 测试等待助手统一（跨 17 文件，公共文件临界区 → 谨慎，或仅 CROSS_TRACK 记录）
- [ ] T3.2 CROSS_TRACK_FINDINGS.md：#1588（preflight）、#1589（science_context）、#1590（analysis sieve）

## REVIEW / 交付
- [ ] R1 独立 reviewer #1（逐文件逆向）
- [ ] R2 独立 reviewer #2（新增代码 + 跨轨风险）
- [ ] REBASE 到最新 origin/master、全量复测、ownership diff 复核
- [ ] PR `hardening(r19-01): data fabric, model runtime and reproducible persistence`
