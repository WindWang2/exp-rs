# hardening(r22-teaching): 评分权威身份化 checkpoint + 跨学生泄露门禁 + NaN/Inf 会话防护 + Windows cmd 注入拒绝（#1694/#1689/#1678/#1688）

- **base**: master `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`
- **head**: agent/r22-teaching-20261025T131928（RED `1610c9b75` + GREEN `70afbb6a8` + 评审修复提交）
- **track docs**: `.planning/r22-teaching/`（BASELINE / ISSUE_TRIAGE / PR_COLLISION_MATRIX / LOOP_LEDGER / TEST_RESULTS / CROSS_TRACK_FINDINGS / REVIEW_R1 / REVIEW_R2 / FINAL_REPORT）

## What & why（全部先 RED 后 GREEN，证据见 TEST_RESULTS.md）

### #1694 (P1) 评分权威身份化 checkpoint
批量评分断点续跑的摘要原本只含版本字符串（且 dock 写死 `ui-1`）：教师改规则文件内容、换 grader CLI 后重跑，旧成绩被原样沿用。现在：
- `BatchAssessmentConfig` 新增 `gradingRulesFingerprint`（规则/评分文件内容 sha256；labid:/none 形式）与 `graderIdentity`（解析后 CLI 路径+大小+内容指纹），进入 checkpoint 摘要；
- 规则内容原地修改 / grader 替换 → 摘要不匹配 → 全量重评，报告发布 `checkpoint_invalidated`，控制台明示"规则已变更，全部重新评分"；
- dock 版本标签来自实际 rubric/lab 文档（`gradingVersionLabels`：显式 `version` 成员或 `fp:<hash12>` 内容指纹），CSV/JSON 不再出现 `ui-1`，成绩可追溯到产生它的规则。

### #1689 (P2) 跨学生泄露防护接入生产路径
- `assertNoCrossStudentLeak` 新增 cohort 感知重载：以本批 roster 的其他学号（纯数字、中文姓名、任意自定义 ID）按词边界匹配（`20230021` 不误报 `2023002`；本人 ID 不自伤）；
- 新增 `buildGuardedFeedbackPacks()`：每个包在输出前过门禁，疑似泄露的包被扣留并计入 typed refusal；`TeachingAdminDock::onBuildFeedbackAndSummary` 生产路径改走此门禁。

### #1678 (P2) NaN/±Inf 数值答案拒绝
`lab::recordAnswer` 的 Numeric 分支拒绝非有限值（`lab.session.field`）；`sessionFromJson` 同样拒绝非有限 `numeric`（jsoncpp 会把 1e999 解析为 inf）——会话文档不再可能被一个异常值毒化到无法重载。

### #1688 (P2) Windows cmd.exe 注入拒绝
`bundleBuilderRequest` 参数 fail-closed 校验：version 白名单 `[0-9A-Za-z._-]`，路径拒绝 `&|<>^%!\"` 与控制字符（`C:\R&D\out`、`1.0&calc`、`%TEMP%` 全拒）；非法输入产出空请求（无 program），`buildOfflineBundle` 返回 typed refusal 而非启动进程。POSIX 孪生路径同策略。与 07 轨 `build_offline_bundle.*` 的 `--version` 修复接口对齐（CROSS_TRACK_FINDINGS #2）。

## Issue 状态
- 修复：#1694 #1689 #1688 #1678（本 PR）
- 验真未重复实现：#1558 已由 `0847c5ec1` 修复（issue 仍 OPEN，可关闭）
- #1696（master 编译失败）属 infra 轨：本轨对 `src/lab/session_state.cpp` 的行为修复经"临时本地 include"验证后**未携带**该 include 入库

## 验证（Windows/MSVC 主机，本地）
- RED（master 行为）：`#1694 resumed==2`、`#1678 NaN accepted`、`#1689 numeric leak ok`、`#1688 cmd.exe argv` 四处真实运行失败记录于 LOOP_LEDGER R4
- GREEN×2：`test_r22_teaching_batch_regrade` 98 断言、`test_r22_teaching_guard_hygiene` 76 断言全绿（两遍）
- 既有族：checkpoint_stamp(85)、grader_cli_budget(3)、r15_E_batch_refusal(62)、rubric_weights(19)、student_id_authority(23) 全绿
- 预存环境红（stash 基线复核同断言失败，非本 PR 回归）：`test_teaching_admin_core` 2 例（MSVC abort()=exit3 非 signal 的 grader_crashed 模拟；python pin probe）、`test_lab_runtime` 1 例（Windows remove_all 句柄竞争）
- dock 改动经 `teaching_admin_dock.cpp` TU 编译验证（sicnu_geo_rs_shell）+ 双评审
- **不等待 CI**（按轨道章程，本地可执行检查为准）

## 与 R21/兄弟轨差异核对
- #1674 (r21-E)：未触碰其文件（output_verifier/faultlab/lab_verify_composer）；`checkpoint_invalidated` 为 additive 字段，`loadBatchReportFromFile` 忽略未知字段
- tests/CMakeLists.txt 仅追加本轨注册块（与 #1674 的追加块互不重叠，rebase 安全）
- 共享构建机事件：vcpkg manifest ABI 风暴删除了共享树 opencv4+protobuf，本轨已修复重装（详见 LOOP_LEDGER R2/R3 与 CROSS_TRACK_FINDINGS #3）

## 遗留风险 / 未执行项
- lab-id 形式的 CLI 内部解析（lab pack 数据变化）对摘要不可见——需 CLI 侧 `--print-authority-fingerprint` 类接口（后续跟进，不阻塞）
- 全仓测试扫描未跑（主机预算）；覆盖了全部触碰产品面的目标族
- GUI 运行时行为（dock 按钮流）无仓内测试基建，以 TU 编译 + 评审覆盖
