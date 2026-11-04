# TASKS — R27-T2

阶段与切片（每切片独立 commit，RED→GREEN）：

- [ ] T0 基建：.gitignore 追加 r27-t02 白名单；测试注册 append-only 块；build 目录配置（-j2 上限）
- [ ] T1 A/#1743：RED（重复/空 join 反例）→ 修复（fail-closed + 状态不变保证）→ 反例矩阵 + O1/O2 用例
- [ ] T2 B/#1766：RED（并发双 execute 无锁竞争）→ `exclusive_path_lock`（flock/LockFileEx）+ typed `ChunkRunLockHeld` → 子进程崩溃恢复用例（O3）
- [ ] T3 B/#1767：RED（stuck worker shutdown 挂起——用 deadline 断言）→ immortal instance + 有界 join + detach → per-job 可选超时 → O4/O5 + RIO-9/RIO-10 用例
- [ ] T4 B/#1764：tracker `trackedCount()` 可观测 → teardown/retire `reset` → O6 用例（含 pool 层）
- [ ] T5 C/#1765：`safeRemoteRasterPath` 后端入口 + 回归契约（O7）→ RIO-6 acquire 接线（O8）
- [ ] T6 P3 清扫：RIO-7（vector_reader SQL）、RIO-11（governance retention）、RIO-12（crs_policy Ex/开关）、RIO-13（mirror v2 拒降级）、RIO-14（query_planner 重复页）（O9）
- [ ] T7 D：import_session 四状态 + 摘要（O10）
- [ ] T8 E：尾批/故障注入/资源有界验证用例（O11）+ BENCH 记录
- [ ] T9 全量目标构建 + 关键测试双跑（O12）+ TEST_RESULTS.md
- [ ] T10 R1 独立评审（并发/资源生命周期视角）→ 修复 → 复跑 Oracle
- [ ] T11 R2 独立评审（地理空间契约 + 安全 I/O 视角）→ 修复 → 复跑 Oracle
- [ ] T12 PR readiness（白名单核验/diff 检查/三方 vs origin/master）→ push → gh pr create
