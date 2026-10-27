# R24-A Tasks（工作包分解；状态随循环更新）

- [x] T0 动态侦察 + 基线文档（BASELINE/ISSUE_TRIAGE/OWNERSHIP/PR_COLLISION_MATRIX/DECISIONS/LOOP_LEDGER）
- [x] T1 worktree + build-r24_a 配置成功（Ninja/MSVC/Debug/TESTS ON；bison/flex=winflexbison）
- [ ] T2 WP-A0 ALGORITHM_INVENTORY.md（家族→注册 id→入口→测试覆盖→基线声明）
- [ ] T3 WP-A1 #1687：RED v1（运行时失败）→ 修复（等值负对语义 + 声明哨兵重载 + kUndeclaredSentinelCeiling 单一所有者 + validCount 契约文档 + spectral_indices isSentinelSample 收编）→ GREEN×2 → 负控
- [ ] T4 WP-A7 help 目录键↔注册一致性 drift 测试（修防腐烂缺口，键名已 FIXED）
- [ ] T5 家族审计结论复核（时序/SAR-地形 代理 + 光谱-分类 主代理自查）→ 立项追加 RED→GREEN×2（按证据充分度排序，宁缺毋滥）
- [ ] T6 WP-A6 性能：识别 1 个可证优化点（拷贝/重复 IO/无界聚合）→ before/after 5 次中位数 + RSS；无证据即 N/A
- [ ] T7 WP-A8 E2E ≥3 条（光学指数链/时序链/DEM 链，真实 GDAL 读写、CRS/nodata/provenance 校验、失败无半文件）
- [ ] T8 邻域回归 ×2（受影响测试族全绿两轮）+ scripts/check_merge_registration_integrity.py --require-refs
- [ ] T9 R1 独立评审（fresh reviewer：数学/边界/调用链）→ 修 P0-P2 → 复跑
- [ ] T10 R2 独立评审（fresh reviewer：干净 diff 全量复核 + merge-loss/平台）→ 修 → 关门
- [ ] T11 PR：fetch 复核碰撞 → push → gh pr create（非 draft，PR_BODY.md）→ FINAL_REPORT.md 回填 URL

## 循环纪律

- 每回合一个聚焦改动；RED→GREEN×2；账本逐轮追加。
- 连续 3 回合失败 → 换假设；8 回合 → 重拆包。
- 子代理结论必须主代理实读复核后才立项。
