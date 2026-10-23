# R20-05 CROSS_TRACK_FINDINGS — 跨轨发现(本轨禁写区,只报告)

## CT-R5-1 [#1536, P1] guided workflow 行号换算选错实验 → 04 轨
- `src/app/widgets/guided_workflow_widget.cpp:168`:`labIndexOfRow(row, errorCount)` 把**一条**错误条目当 errorCount 条换算;errorCount≥2 时每个实验行索引整体偏移:第 1 行得 -1 被 :193 静默丢弃、第 2 行得 0 打开第 1 个实验——点选与运行的实验不一致。lab_spec_loader.cpp:521-532 确可累加多个错误。
- 归属:`src/app/widgets/` 非 teaching 子树(04 轨写区)。修复建议:`row - 1` 或记录真实错误行数常量。E10(实验工作台交互一致)语义上相关,但文件所有权优先。

## CT-R5-2 [#1601, P2] 双 verifier 核心的归属矛盾 → 需 maintainer 架构决策
- 实测(master 28e1891390):`src/verification`(ADR 0172 命名的"single place")无任何生产 TU include(仅 tests/CMakeLists 15880/15902 链接);生产链是 `src/verify`(sicnu_verifier)← `src/verify_adapters` ← faultlab/workflow_checkpoint。
- ADR 0172 文本与 src/verify 头部自我归属、ADR 0174、docs/development 两处 recon 文档互相矛盾;`src/verify/verify_sha256.cpp` 是第二份 SHA-256。
- 本轨未动(提示词 §6 明令禁止无证据大重构/去兼容入口)。建议:要么由架构所有者更新 ADR 0172 承认 src/verify 为生产权威并给 src/verification 定位(测试沙盒/演进核),要么规划合并;SHA-256 收敛可随行。

## CT-R5-3 [#1607 同类缺口] src/recipes 两处 → 未明确分轨
- `src/recipes/lab_document.cpp:283`(钉 256 但未捕获)、`src/recipes/provider_interfaces.cpp:62-67`(两者皆无)。recipes 不在本轨白名单。
- 本轨已修同类:src/teaching 五处(lab_session_state【既有】/lab_completion_ledger/verdict_stamp/lab_operator_launch×2/lab_cockpit_dock)。

## CT-R5-4 [R19-02 CT-1 剩余] merge c8f5e0805422 其他丢失路径 → 03/04 轨
- 本轨已恢复 teaching 侧全部(course_home_view_model、lab_feedback_projection、batch_assessment 记账+加载器、teaching_admin_dock、两个测试文件)。
- 仍丢失(==merge-base):`src/app/shell/{rs_job_panel,task_panel_host,workflow_session_controller}.cpp/.h`、`src/app/widgets/rs_result_summary.cpp`、`src/help/operator_help_provider.cpp`、`data/help/operators/{data_io,optical}.json`、`tests/test_rs_result_summary.cpp`、`tests/test_r15_E_{result_artifact_honesty,task_panel_state,progress_text,lab_pack_bounds}.cpp`(后三者对应生产侧 app/shell 与 help 改动,由 03/04 轨判断恢复)。
- 检测命令:`git diff --name-only c8f5e0805422^1..c8f5e0805422^2` 后逐路径三方核对(脚本见 R19-02 BASELINE 或直接 bash 循环)。

## CT-R5-5 [观察] grader passingScore 默认值与 totalPoints 校验的组合
- `GradingRubric` 默认 passingScore=60(grader_types.h:272),validate 要求 passingScore≤totalPoints;省略成员且 totalPoints<60 的 rubric 被拒("passingScore must be within [0, totalPoints]")。fail-closed 且类型化,但"省略即默认 60"对小分制 rubric 作者是个隐蔽陷阱(E05 后续可考虑:成员缺失时默认改为 totalPoints 的比例或跳过该校验——需 grader 域证据与评审,本轨未动)。

## 本轨区内的相邻候选(后续轮,未处理)
- #1652 教学管理台其余技术债项(若 #1652-1 修复后 issue 仍有残余条目可随之关闭)
- E12 测试可信度扫域:tests/test_e2e_open_issues.cpp 内可能仍有其他弱断言用例(本轮只验证了 #1558 两处已修)
