## R26-03 harness：证据闭环 fail-closed、planner 事实门、capability family 漂移治理

base: `master` @ 2610cb4741e69a89124921e44c03e340e2522ee2
head: `agent/r26-harness-20261030T151107Z` @ a023967a47（3 commits）

### 范围（src/agent/harness + src/planner + tests + 生成数据）
深化既有 Pi/MCP Harness 的「规划—执行—校验—恢复」闭环可靠性，聚焦可证实的 fail-open/虚假成功路径：

1. **验证证据 fail-closed**（plan_tools/evidence）
   - verification 证据 sidecar 写失败：warning → error 级，run 读 `status=failed`（此前 "completed" 声明无证据背书）
   - 新增 `inspectVerificationEvidence` 三态（Absent/Reusable/ForeignRun/Corrupt）+ 全字段类型门与 Json::Exception 兜底：损坏/类型错乱/不可归属的本 run 证据一律 typed FAIL 且**字节保留供审计**，poll 不再静默重算翻转 verdict
   - provenance：仅「kind=harness_provenance 且 run_id 不同」的陈旧 harness 信封随新 run 原子替换（修「新工件配旧 run provenance」）；引擎 DerivationRecord 永不覆盖（kind 信封判别 + 空 run_id fail-closed）
2. **诊断面一致性**（run_loop）：`harness:diagnose_run` 现以 `verification_evidence_integrity` 失败条目暴露损坏证据（此前报 completed/无 failures，把成功结论交给修复循环）
3. **会话/ledger 真值**（context_checkpoint/context_ledger）：`workflow_session delete` 失败返回 typed failure（此前 success 藏 ok:false）；ledger 重绑刷新非空 plan_id/goal/intent（空值保留旧身份，状态-only 更新语义不变）
4. **Planner 事实推导门**（planner_core）：未知 CRS（>1 consumable）→ blocking question（修 "「」+EPSG 读作同网格」）；`wavelengthPolicy=band_roles` 契约算子遇未声明 band_roles 资产 → blocking question + feasible_with_gaps（bandRoles 事实此前解析后弃用）
5. **Capability family 漂移**（capability_catalog + 生成数据）：familyMap 补 planner slotTable 遗漏的 10 算子（rs:register_images/rs:stack_register 曾被静默归 io、rs:change 归 classification）；同步 4 个 sidecar family 字段并重生成 pi/knowledge 页面；新增漂移守卫（unknown-group→io 默认值 + 10 pin）
6. 杂项：harness_error 删除重复 WAVELENGTH_INCOMPATIBLE 条目

### 验证（RED→GREEN×2，本地 Debug -j4 + flock 宿主锁）
- 3 个新 Oracle 套件：`test_r26_harness_evidence_integrity`（71 断言/8 用例，含 A1/A2/A2c/A3/A3b/A3c/B/C）、`test_r26_planner_fact_gates`（16/2）、`test_r26_capability_family_drift`（12/2）
- RED：修复前二进制上 22 断言失败（每条 Oracle 均先在真实生产实现复现）
- GREEN×2：连跑两遍全绿；**5 个回归变异**（severity 回退/provenance 替换禁用/Corrupt 禁用/familyMap 删条/band-role 门移除）全部被对应测试捕获
- 既有回归：22 个受影响目标全绿（含 test_capability_knowledge 1637、test_capability_completeness 1104、test_grounding_evidence_r4 88、test_context_ledger_completeness_r4 55 等）；test_scientific_planner_core/constraints 两 fixture 补声明 band_roles 保持原 spine/budget 意图
- `scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`：+3 目标 / 0 丢失 / 0 冲突标记
- `capability_knowledge_tool gen-pages --check` zero diff；`gen-meta` 重生成 157 sidecar 后 git status 干净

### Issue triage（详见 .planning/zcode-r26-harness/ISSUE_TRIAGE.md）
#1539/#1540/#1552/#1555/#1556/#1690/#1691/#1692/#1704 均 **FIXED_IN_MASTER_BUT_OPEN**（含 file:line 证据）；#1628/#1629/#1640/#1650/#1693 为 CROSS_TRACK/OUT_OF_SCOPE。本轨未重复任何已修项。

### 独立评审
R1（未参与实现子代理）：无 P0/P1；P2×2/P3×3/P1×1 全部处置（commit b714b2cf7）
R2（全新上下文子代理）：无 P0/P1；P2×1（类型门）+ P3×3 全部处置（commit a023967a47）
评审全文与处置：`.planning/zcode-r26-harness/REVIEW_R1.md` / `REVIEW_R2.md`

### 未运行 / 已知限制
- NOT_EXECUTED：GPU/模型权重、Windows/macOS、远程系统、真实 `--mcp` 端到端（mcp_server/pi 桥属 #1727 只读区，本轨零触碰）
- test_capability_surface_parity 失败为**预先存在**（glossary 资源未编入该 target；master baseline 同失败，与本轨 diff 无关）
- planner 两门当前只被测试触达：planScientificWork 无生产调用方（遗留接线缺口，science_context 投影未填 assets）——接线时须同步投影 bandRoles（RISK_REGISTER 登记）
- provenance exists-then-write 竞态窗口（R1 取舍记录于 RISK_REGISTER）

### 跨轨
- 与 #1724/#1725/#1726/#1727/#1728 零文件交集；与同轮 r26-workbench / r26-science 零文件交集（仅 tests/CMakeLists.txt 尾部各自 append-only 块）
- tests/CMakeLists.txt：仅追加 `# BEGIN R26-harness` 块；surface_diff_snapshot.json 未触碰
