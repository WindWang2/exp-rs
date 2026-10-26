# R21-E FINAL_REPORT — 本科遥感实验教学、自动评分、科学验证与帮助诊断轨

**PR**: https://github.com/WindWang2/exp-rs/pull/1674（OPEN，未自动合并、未启用 auto-merge、未等待线上 CI）
**分支**: `agent/r21-e-teaching-verification-20261023T215154Z`（head `600c2348`，10 提交，41 文件，+约 2600/−约 40）
**BASE**: origin/master `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（gh API LIVE 复核；git 传输层在本宿主不可达，分支经 GitHub Git Database API 逐对象发布，远端树与本地 HEAD 逐字节一致）

## 一、LIVE 盘点结论（O-G1）
- 远程 master = 911e0b17（PR #1672 之后无漂移）；开放 PR 基线 = 0；Open Issues = 126 全量拉取并逐条代码级分诊（`ISSUE_TRIAGE.md`）。
- **R20-05（PR #1668）13 项修复声明全部在当前 master 实锤**（#1558/#1593/#1598/#1599/#1600/#1602/#1607/#1610-#1613/#1652/#1653 + merge-loss 恢复）——不重复修，给出 close 建议与证据。
- 复测另发现 **#1614/#1615/#1616/#1639/#1636 亦已修未关**（中间轮修复），零重复 commit。
- **#1601 考古结论**：权威核 = `src/verify`（sicnu_verifier），但生产消费为零；`projections.h` 两个投影仅测试接线；生产评分走第三套面 `agent::OutputVerifier`。处置 = 生产接线而非搬库/删库。

## 二、交付物（全部 RED→GREEN×2）
1. **W1（#1601）验证器生产接线**：`verify_adapters/lab_verify_composer`（默认教学 spec：exists/type/grid pins，从规则 crs_grid 派生；fs+GDAL probe；三值投影+封印）；`LabGradeResult::verifier` additive 成员 + 6 挂载点（覆盖全部 artifact-class 拒绝路径；usage 拒绝不挂）；CLI 报告零改动自动携带；三个叶库加 PIC。
2. **W2（#1613）FaultLab 声明式验证闭环**：严格 parse（闭合形状、rules_ref↔非空 must_fail 互为必需）；loader 从拒绝改为接受+校验；runner 第 7 合取项 + 注入式钩子（依赖倒置保叶库纯净）+ 五种 fail-closed 诊断 + 异常守卫 + 早退可见性；studio_live 生产钩子（FaultGrid→RasterWriter 原子物化→gradeForTeaching→must_fail⊆观测失败集）。
3. **W5（O-E2）批量压力**：100/500/1000 份 lane——行数不变式、runner 真实四分类记账、duplicates 打标不消失、重跑 byte 等同（中断治愈）、耗时/RSS 证据记录（不作门）。
4. **W6（O-E1/O-E5）离线端到端**：样例生成→NDVI pipeline→grade（**verifier 证据 status=pass 出现在生产 CLI 报告**）→15 人批量（2 坏件 unverifiable 隔离）→审计 CSV→重跑 digest/verifier byte 等同。**ALL E2E STEPS PASSED**。
5. **W3/W4**：help/explain 五项确认已修；70 个域内测试文件弱断言扫描零命中。

## 三、评审闭环（O-G4）
- **R1**（双 fresh subagent）：REQUEST-CHANGES → 全部处置（rulesDir 约定、kernel-walk 挂载、declared 谓词对齐、vacuous 拒绝、死成员、早退可见性、scale/offset）。
- **R2**（fresh subagent，自行运行测试）：READY-WITH-FIXES → **P0-1/P1-1/P1-2/P2-1..4/P3-1..4 全部处置**（含把批量记账不变式改为 runner 真实契约、studio 测试诚实化+控制组、PROJ 探针、digest 预算 0、schema 收紧、skipped_no_fault 覆盖）。
- 复测：四 lane GREEN×2（22/66/31/11 断言）+ 28 目标回归 sweep 全绿 + e2e PASS。

## 四、硬性 Oracle 对账
| Oracle | 状态 |
|---|---|
| O-E1 学生全流程 + 三值与证据一致 | ✅ e2e + 4 lane |
| O-E2 批量幂等/压力/RSS | ✅ 100/500/1000 + 记账不变式 |
| O-E3 verify/adapter/FaultLab 契约可解释、坏输入不"可信通过" | ✅ 生产调用有测试；fail-closed 全形状 |
| O-E4 R20 回归两遍绿 + 已修未关复测 | ✅ 28 目标全绿；13+5 项复测证据 |
| O-E5 离线零 egress/可复现 | ✅ SICNU_OFFLINE=1 全链路；重跑 byte 等同 |
| O-G1..G7 | ✅ 盘点/分诊/冲突矩阵；RED→GREEN×2；性能证据；双审；残余诚实归因；41 文件全白名单；无 TODO/假实现 |

## 五、残余与 NOT_EXECUTED（诚实归因）
- **Windows 实机** run_lab.cmd/grade_all.cmd：NOT_EXECUTED（宿主 Linux；Linux 等价链路全绿不冒充 Windows）。
- macOS / TSan / ASan / 全量 CMake 域 sweep：NOT_EXECUTED（共享构建锁 + 宿主面限制）。
- **CT-6（重要发现）**：文档化离线 lab1 流程（生成 256×256 EPSG:32648 场景 → ndvi_basics 规则钉 128×128 EPSG:4326）得 45/100——学生首次上课即见 FAIL。已实证记录，期望所有者 = 教学内容/tools；本轨不越权改灰区。
- 构建环境事实：vendor 前缀静态库（zlib/expat/sqlite）无 -fPIC，需钉系统共享库 + `--allow-shlib-undefined`；armadillo 等缺失依赖复用兄弟轨 link-shim（CT-1/CT-2 记录）。

## 六、下一批恢复点
`.planning/r21-E/`：TASKS.md（剩余 backlog）、CROSS_TRACK_FINDINGS.md（CT-1..6）、TEST_RESULTS.md（复现命令与环境）、RISK_REGISTER.md（R1-R7）。工作树 `/home/kevin/project/exp-rs-r21-E-20261023T215154Z` 保持可构建状态（build-r21-E）。

## 七、Issue close 建议（仅可核验建议，未擅自关闭）
#1601 #1613（本 PR 修复）；#1558 #1593 #1598 #1599 #1600 #1602 #1607 #1610 #1611 #1612 #1614 #1615 #1616 #1636 #1639 #1652 #1653（master 复测已修，证据在 ISSUE_TRIAGE.md）。#1601 的 ADR 0172 归属正文更新留 maintainer（CT-5）。
