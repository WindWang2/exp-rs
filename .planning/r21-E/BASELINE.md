# R21-E BASELINE — 教学轨（本科遥感实验教学 / 自动评分 / 科学验证 / 帮助诊断）

采集时间：2026-10-23T21:52Z（执行时刻 LIVE）· 采集人：R21-E 主代理

## Git 基线
- **BASE = origin/master = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`**（merge PR #1672）
  - 本地 fetch 实测失败（github.com:443 直连与 socks5h://127.0.0.1:7890 均超时/SSL EOF），
    但 `gh api repos/WindWang2/exp-rs/branches/master` LIVE 查询返回同 SHA ⇒ 本地 origin/master 引用与远程一致、无漂移。push 通道风险见 RISK_REGISTER。
- worktree：`/home/kevin/project/exp-rs-r21-E-20261023T215154Z`，branch `agent/r21-e-teaching-verification-20261023T215154Z`
- 主仓工作树（master @ 28e1891390，落后 59）只读未动。
- Open PR = 0（`gh pr list --state open` 实测）。最近合并：#1672/#1671/#1670/#1669/#1668（R20 五轨）。
- 兄弟轨 worktree 在场：r21-A（算法）、r21-B（数据/模型）、r21-C（Pi/MCP）。旧 r20-* 分支只有 behind 无 ahead，未用作基座。

## Issue 基线
- Open Issues = 126（gh 全量拉取，/tmp/r21e-issues-1.json）。
- 大构成：#1535–#1658 为 2026-10 swarm 评审批次（P1 ×~24、P2 ×~70、P3 技术债 ×~39）+ 3 个旧批（#1467 #1491 #1558 前）。
- 本轨域内（teaching/teaching_admin/lab/lab_pack/grader/verification/verify/verify_adapters/faultlab/help/explain-教学相关）共 **21 条**，详见 ISSUE_TRIAGE.md。

## R20-05（PR #1668）交付复测 — 全部实锤在当前 master
| 项 | 结论 | 证据（master 911e0b17 实读） |
|---|---|---|
| #1607 jsoncpp 钉定+typed catch | ✅已修（全站点） | lab_pack.cpp:249=128、session_store.cpp:74=64、verdict_stamp.cpp:136=256、lab_completion_ledger.cpp:166=256、lab_session_state.cpp:234=256、grader_json.cpp:252=256 |
| #1652-1 rubric 委托 | ✅已修 | rubric_builder.cpp:197 → `GradingRubric::fromJson` |
| #1652-2 cross-student 泄露误报 | ✅已修 | feedback_pack.cpp:44-47（`student_id` mismatch 检查） |
| #1653 beginResult 去重 | ✅已修 | checks_numeric.h:69 导出，checks_* 复用 |
| #1600 采样盖章 | ✅已修 | verify_engine.cpp:461-462 写 hasSampling/sampledPoints；gdal_grid_probe 记 sampledPixels |
| #1598 derivedStatus 折叠 uncovered | ✅已修 | render_teaching.cpp:253-297 contributingStatus/derivedStatus |
| #1599 builtin pack 词表 | ✅已修 | packs_builtin.cpp:119/156/165/187（expect.kind/required_dimensions/expected.algorithm） |
| #1602 bounded_io 按大小分配 | ✅已修 | bounded_io.cpp:53/72/80-81 |
| #1593 lab_pack stackLimit | ✅已修 | lab_pack.cpp:249 stackLimit=128 |
| #1610 delta 基线缺失 fail-closed | ✅已修 | fault_expectations.cpp:37/54-57 |
| #1611 index_pair isString 门 | ✅已修 | fault_observables.cpp:144-145 |
| #1612 kappa 原下标配对 | ✅已修 | fault_observables.cpp:277-298（注释明示过滤向量 vs 原数组） |
| #1613 空 verifier 拒绝 | ✅已修（但见下） | fault_scenario.cpp:426-429 拒绝非空 expected.verifier |
| #1558 恒真断言 | ✅已修 | 全仓 `CHECK( true` 仅剩 mutation_kill_11.cpp:300 `truePositives == 4`（真断言） |
| WP-ML merge-loss 恢复 | ✅在 master | course_home 钳制/feedback 投影 indeterminate/batch fail-closed/两测试文件恢复（R20-05 报告+抽读） |

## #1601 双核考古（本轨 E3-8 核心）
**当前 master 的事实（非推测）：**
1. `src/verify`（target `sicnu_verifier`）= **权威**。CMakeLists 自述 "The one verification truth for student experiments, agent execution and batch acceptance"（ADR 0172 归属注记已校正）。
2. `src/verification`（target `sicnu_verification`）= **平行旧核**。CMakeLists split notice 明示 "no production TU includes its headers and it is wired only for its test suite (test_verifier_* lanes)"；测试线 tests/CMakeLists.txt:16038-16328。
3. 生产消费链（链接级全查）：`sicnu_verifier` 的链接者只有 `sicnu_verify_adapters(+_gdal)` 与 tests（verifier lanes、evidence seam、14467、15717）；`src/explain` CMakeLists 中的命中属于**禁止链接守卫列表**（层 guard），非链接。⇒ **权威核当前零生产可执行消费**。
4. `src/verify/projections.h` 已备好两个纯投影：`harnessVerificationFromReport`（harness 4.0 ArtifactVerification 形状、Indeterminate→FAIL 保守投影）与 `labEvidenceFromReport`（OutputVerifier::LabEvidence 字段名）。**仅 test_evidence_seam.cpp 消费**（"the only place they meet"）。
5. 生产评分真路径用的是**第三套验证面**：`sicnu::agent::OutputVerifier`（src/agent，Qt 侧），lab_batch_runner.cpp / lab --grade 消费 LabGradeResult/LabDeduction。
6. faultlab（#1613）的 `expected.verifier`（rules_ref/must_fail_assertions）被 schema v1 拒绝（fault_scenario.cpp:426-429）——确认未接线，schema 注释明说 "schema v1 has no artifact-level verifier"。

**⇒ R21-E 的 #1601 实质工作不是搬库/删库，而是把「权威核 → 生产证据」的最后一公里接通：** 让生产 lab 评分路径真实运行 sicnu_verifier pack 并把投影报告落进 grader `verifier_verdict` evidence（EvidenceKind 已存在，grader_types.cpp:451），教学端渲染一致。禁止覆盖 R20 已修的 derivedStatus/builtin packs。

## 离线课堂链路（E1-3/E4 现状）
- `scripts/windows/run_lab.cmd`（三步：样例生成→NDVI pipeline→lab --grade）与 `grade_all.cmd`（批量→CSV，错误行隔离）在仓。
- Linux 等价入口：`sicnu_geo_rs_cli --offline lab --lab ndvi_basics --grade/--batch/--csv`（lab_batch_runner.cpp，637 行，含 symlink 拒绝/CSV 公式中和/原子 sidecar 写）。
- 打包：packaging/bundle + OFFLINE_BUNDLE.md。

## 构建基线
- 依赖路径复刻 r21-A：GDAL/PROJ/GEOS/GSL ← `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix`，Qt6.12 系统。
- 首配置三次缺依赖（GDAL→PROJ/GEOS→GSL），逐项补 -D 后进行中；详见 LOOP_LEDGER。
