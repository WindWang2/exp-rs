# R21-E ISSUE_TRIAGE — 126 open issues 分诊（LIVE 2026-10-23，master 911e0b17）

分类口径：OPEN-UNFIXED（在 master 可 RED）/ FIXED-OPEN（已修未关，本轨复测证据）/ PARTIAL / OBSOLETE / DUPLICATE / NEEDS-EVIDENCE / OUT-OF-SCOPE。
行号均为 master 911e0b17 实读。

## 本轨域内（21 条）

| Issue | 级 | 路径 | 分诊 | 证据/处置 |
|---|---|---|---|---|
| #1601 | P2 | src/verification vs src/verify | **PARTIAL（架构已定、生产缺口未接）** | split notice 在两处 CMakeLists；权威=src/verify；但 sicnu_verifier 零生产消费、projections.h 仅测试接线、生产评分走 agent OutputVerifier。**R21-E 主工作项**：接通 verifier→grader verifier_verdict evidence 生产路径 |
| #1613 | P2 | src/faultlab | **PARTIAL（拒绝≠接线）** | fault_scenario.cpp:426-429 拒绝非空 expected.verifier（schema v1 无 artifact-level verifier）。rules_ref/must_fail_assertions 只在 data/faultlab/faults.schema.json:160-171 声明。**R21-E 工作项**：schema v2 语义或删字段，禁止 accepted-but-inert |
| #1607 | P2 | src/teaching | **FIXED-OPEN** | 6 站点全部 pin+catch（见 BASELINE 表）；建议关闭 |
| #1600 | P2 | src/verify | **FIXED-OPEN** | 采样盖章已产（verify_engine.cpp:461）；建议关闭 |
| #1598 | P2 | src/verification | **FIXED-OPEN** | derivedStatus 折叠已修（render_teaching.cpp:253-297）；建议关闭 |
| #1599 | P2 | src/verification | **FIXED-OPEN** | 词表已修（packs_builtin.cpp:119+）；建议关闭 |
| #1602 | P2 | src/verify_adapters | **FIXED-OPEN** | bounded_io.cpp:50-81 按大小分配；建议关闭 |
| #1593 | P2 | src/lab_pack | **FIXED-OPEN** | stackLimit=128+catch；建议关闭 |
| #1610 | P2 | src/faultlab | **FIXED-OPEN** | delta 基线缺失 fail-closed；建议关闭 |
| #1611 | P2 | src/faultlab | **FIXED-OPEN** | isString 门；建议关闭 |
| #1612 | P2 | src/faultlab | **FIXED-OPEN** | 原下标配对；建议关闭 |
| #1652 | P3 | src/teaching_admin（2 项） | **FIXED-OPEN** | -1 委托 GradingRubric::fromJson；-2 student_ 前缀比较；建议关闭 |
| #1653 | P3 | src/verification | **FIXED-OPEN** | beginResult 导出复用；建议关闭 |
| #1614 | P2 | src/help | **FIXED-OPEN（复测实锤）** | help_presenter.cpp:164-180 预算兜底改为对象内降级（drop title→elide id→`"{}"`），注释明示 "never by slicing"；与 issue 描述的 `idOnly.left()` 已不同。建议关闭 |
| #1615 | P2 | src/help | **FIXED-OPEN（复测实锤）** | error_diagnostics_bridge.cpp:61-77 `thread_local CuratedCodeTable memo` + registry/count/alias 失效感知重建 + families 排序。建议关闭 |
| #1616 | P2 | src/explain | **FIXED-OPEN（复测实锤）** | explanation_builder.cpp:36-45 `integralRepresentable`（isfinite+ll 范围）门已加。建议关闭 |
| #1639 | P3 | src/help（2 项） | **FIXED-OPEN（复测实锤）** | -1 help_presenter.cpp:17-22 elide 复用 truncateSummary；-2 terminology_provider.cpp:227-228 `truncateSummary(term.definitionZh)`（注释引用 #1639）。建议关闭 |
| #1636 | P3 | src/explain | **FIXED-OPEN（复测实锤）** | explanation_validator.{h,cpp} 全文无 `guidance_` 命中——死成员已删。建议关闭 |
| #1648 | P3 | src/recipes | **OUT-OF-SCOPE→CROSS_TRACK** | R20-05 已移交（lab_document.cpp:283 / provider_interfaces.cpp:62-67 非本轨白名单）；记录不抢修 |

## 域外（105 条）——不修，只记冲突面
- P1 批（#1535–#1557）与 P2 批（#1574–#1606 等）全部属算法/运行时/数据/代理/工作台/CLI 轨：OUT-OF-SCOPE。其中与教学面交界：#1582（experiment 评测跳行）归实验轨；#1594-#1597（cli flag 解析）归 CLI 轨（grade_all.cmd 调 CLI，但其修在 src/cli 非本轨）。
- P3 技术债批次 #1625–#1658：模块归属明确，除上表本轨 4 条外均 OUT-OF-SCOPE。
- #1491（CI 无完成结果）：maintainer；本轨不依赖线上 CI（协议禁等 CI）。
- #1467（core-fork local-modification cluster）：跨数据/算子轨。
- #1558（P1 恒真断言）：FIXED-OPEN（全仓 CHECK(true) 扫描仅剩真断言）；E3-10 弱断言扫描会再扩展验证。

## 动态说明
- 其他 R21 轨可能随时开 PR；提交前将重跑 `gh pr list --state open` + `git diff origin/master...HEAD --name-only` 复核（O-G6）。
