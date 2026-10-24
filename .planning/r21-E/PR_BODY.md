# hardening(r21-E): 教学轨 — 统一科学验证器生产接线 + FaultLab 声明式验证闭环 + 批量压力证据（双轮独立评审）

## 基线
- **BASE**: origin/master `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（merge PR #1672；gh API LIVE 复核，本地 fetch 通道不可用见 RISK_REGISTER R1）
- **HEAD**: `<本 PR head>` · 分支 `agent/r21-e-teaching-verification-20261023T215154Z`
- 开放 PR 基线 = 0；提交前复查无同域新 PR。

## 读过的 PR / issues
- R20-05 交付：PR **#1668**（merge-loss 恢复/rubric 真值/采样盖章/jsoncpp 钉定）及其 `.planning/zcode-r20-05/*` 五件；PR #1664（r18-e 教学真值）。
- E0 复测（master 911e0b17 实读）：#1558 #1593 #1598 #1599 #1600 #1602 #1607 #1610 #1611 #1612 #1613(拒绝侧) #1652 #1653 **全部已修未关** → 建议 close（证据见 `.planning/r21-E/ISSUE_TRIAGE.md` / `BASELINE.md`）。
- **#1614 #1615 #1616 #1639 #1636 复测发现亦已修**（中间轮已修未关；本轨零重复 commit）。
- 本 PR 直接修复：**#1601**（生产接线语义）、**#1613**（声明式验证真实接线）。

## 工作包（全部 RED→GREEN，详见 `.planning/r21-E/RED_GREEN.md`）
1. **W1（#1601）verifier 生产接线**：ADR 0172 权威核（src/verify）此前零生产消费。新增 `src/verify_adapters/lab_verify_composer`（默认教学 spec：exists/type/grid-pins；fs+GDAL probe；三值投影+spec 封印）；`OutputVerifier::LabGradeResult` 增 additive `verifier` 证据字段（6 挂载点含 artifact-class 拒绝路径；usage 拒绝不带证据）；CLI `lab --grade/--batch` 报告**零 CLI 改动**自动携带。grid pins 从 rules 的 crs_grid 断言派生——权威引擎独立复检评分器已判的网格契约。
2. **W2（#1613）faultlab 声明式验证**：schema 文档化的 `expected.verifier`（rules_ref/must_fail_assertions）从"loader 拒绝"变为真实契约——严格 parse（闭合形状/must_fail 需 rules_ref）、runner 第 7 合取项、**无钩子声明即 fail-closed**（`faultlab.verifier_missing`）、钩子异常隔离（`verifier_did_not_run`）、fault 未注入跳过（Warning）。生产钩子在 studio_live 组合层（FaultGrid→RasterWriter 原子物化→gradeForTeaching→must_fail⊆failed ids）——faultlab 叶库保持 jsoncpp-only（依赖倒置）。
3. **W5（O-E2）批量压力**：100/500/1000 份确定性模拟提交 lane——行数不变式（rows==total）、记账分割（total==graded+isolated+unavailable；duplicates⊆graded 打标不消失）、重跑 byte 等同（中断治愈/无叠加）、耗时+RSS HWM 记录（不作时间门，#1622 纪律）。
4. **W6（O-E1/O-E5）Linux 端到端**：`bundle 素材 → 样例生成 → NDVI pipeline → lab --grade（断言 verifier 证据在场且 status=pass）→ 15 人批量（2 坏件隔离）→ 审计 CSV → 重跑 digest/verifier byte 等同`。

## 不修清单（E0 纪律：已修不重写）
#1558/#1593/#1598/#1599/#1600/#1602/#1607/#1610-#1613(拒绝侧)/#1652/#1653/#1614/#1615/#1616/#1639/#1636 —— master 复测全绿，逐条证据在分诊表。

## 回归与证据
- 受影响族：lab_grading/lab_batch/classroom_safety/lab_scale/lab_offline_e2e/teaching/grader/faultlab(5 lane)/verifier(全部 lane)/evidence_seam/studio_live_e2e/help —— GREEN×2（`TEST_RESULTS.md` 逐条）。
- 弱断言扫描（#1558 延伸）：70 个域内测试文件 CHECK(true)/真空断言/全宽 epsilon/空 catch **零命中**。
- 兼容性：未声明 verifier 的 faultlab 报告 byte 形状不变（条件序列化）；usage 拒绝的 grade 报告无新字段；无存档 digest fixture（全部同跑比对）。

## 评审（两轮独立 fresh reviewer，全部处置并复测）
- **R1**（fresh subagent ×2，只读）：见 `REVIEW_R1.md`。发现经处置：测试 rulesDir 约定、kernel-walk artifact 挂载、runner/loader declared 谓词对齐、vacuous 声明拒绝、死成员删除、早退可见性、scale/offset 物化。
- **R2**（fresh subagent，78 调用，自行运行四个新二进制）：**READY-WITH-FIXES** → 见 `REVIEW_R2.md`。P0-1（批量记账不变式改为 runner 真实四分类，含 unverifiable 桶；不越权改 src/cli）、P1-1（studio 测试改诚实主张 + 控制组使折叠非空洞）、P1-2（PROJ 能力探针 skip）、P2-1..4（digest 预算 0 / 统一拒绝形状 / 路径耦合文档化 / usage 决策文档化）、P3-1..4（schema 收紧 / 复检范围文档化 / skipped_no_fault 覆盖）。
- 复测：四 lane **GREEN×2**（22 / 66 / 31 / 11 断言）+ 28 目标回归 sweep **全绿** + e2e **ALL PASSED**。

## 残余与 NOT_EXECUTED（诚实归因）
- Windows 实机 run_lab.cmd/grade_all.cmd：NOT_EXECUTED（宿主为 Linux；Linux 等价链路全绿不冒充 Windows）。
- macOS/TSan/ASan/全量 CMake 域 sweep：NOT_EXECUTED（共享构建锁与宿主面限制，逐条记录于 TEST_RESULTS.md）。
- `src/cli` 零改动（白名单外）；CLI 接线需求为零（报告自动携带）。#1601 若 maintainer 欲更新 ADR 0172 归属正文，见 CROSS_TRACK_FINDINGS CT-5。

## 文件白名单对账
全部变更位于：`src/verify_adapters/**` `src/agent/output_verifier.{h,cpp}`（教学评分器最小胶水）`src/agent/CMakeLists.txt`（+1 link）`src/faultlab/**` `src/experiment_studio/live/**` `tests/test_r21_e_*` `tests/CMakeLists.txt`（append-only 尾部）`.planning/r21-E/**`。
