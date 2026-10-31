# RISK_REGISTER — R25-C

| 风险 | 等级 | 缓解 |
|---|---|---|
| surface snapshot 过期（schema 变更） | 高 | 构建后跑 surface_snapshot_regen 并提交再生文件；pi surface 测试验证 |
| 并行轨 A/B 同时改 tests/CMakeLists.txt | 中 | 仅末尾追加标记块；push 前 fetch 合并并验证并集 |
| qgis_gui 全量构建耗时长（-j2 共享锁） | 中 | 后台排队；期间推进文档/W7 脚本 |
| r18c id_null 预存 flake 干扰 GREEN×2 | 中 | 记录基线复现率（2/8）；本轨验证以单文件运行为准 + 全套件注明归属 |
| E2E 崩溃链时序（Running 观测后杀进程） | 低 | 轮询到 step running 才杀；100 slices=10s 预算 |
| resume 后 run 状态墙拒绝（Completed 误判） | 低 | 断言 resume 结果非 error + 完成轮询；生产状态墙为权威 |
