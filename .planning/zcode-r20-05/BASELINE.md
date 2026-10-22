# R20-05 BASELINE — 遥感本科实验教学、自动评测、证据与测试可信度收敛

## LIVE 勘查记录(2026-10-23,全部实拉实读)

- 命令摘要:`pwd; git remote -v; git status --short`(clean)
- `git fetch --prune origin && git rev-parse origin/master` → **`28e1891390bb812dc7ef2fe045ac90492c9525f1`**(与审计快照一致,无新提交)
- `git log origin/master -n 20`:顶部为 #1667(r19-01)/#1666(r19-02)/#1665(r19-03)/#1664(r18-e)/#1663(r18-a) merge;PR #1662–#1667 全部 MERGED
- `gh pr list --state open` → **空**(无开放 PR);origin 远程分支仅 `master`(prune 后)
- `gh issue list --state open --limit 500` → 126 条;逐条读取与本轨相关正文(#1558/#1601/#1598/#1599/#1600/#1602/#1607/#1593/#1610–#1613/#1652/#1653/#1536)
- `git worktree list`:sibling r20-01(无提交)/r20-02(领先1,test seeds)/r20-03(领先6)在各自 worktree;**无 r20-04/r20-05 worktree**,本轨创建:
  - Branch: `agent/r20-05-teaching-verification-20261022T190641Z`
  - Worktree: `/home/kevin/projects/rs-studio/exp-rs-agent/r20-05-teaching-verification-20261022T190641Z`
  - BASE SHA = HEAD = `28e1891390`(origin/master)
- 必读文档:CLAUDE.md、docs/repo-layout.md、CONTEXT.md(要点)、.planning/r19-01|02|03 FINAL_REPORT + r19-02 CROSS_TRACK_FINDINGS、根 .goal-loop-ledger.md(只读尾部)
- merge-loss:scripts/dev/merge_loss_audit.py 可用;CT-1 涉及 `src/teaching/course_home_view_model.cpp`、`src/teaching_admin/batch_assessment.h` 旧合并丢失(R19-02 报告),本轮 triage 中单独核对(见 ISSUE_TRIAGE)

## 构建环境(本机实测,复刻 r19-01/02 配方 + r20-02 系统依赖路线)

- 主机:Arch CachyOS x86_64, Linux LTS, ~40 核 / 62 GB(全局锁 -j2 上限,绝不 -j4+)
- 全局锁:`/home/kevin/projects/rs-studio/main/.git/exp-rs-zcode-global-build.lock`(与 r20-01/02/03 实测互斥;本轮 configure 曾排队于 r20-02 构建之后)
- 配方:cmake + Unix Makefiles, Debug, ENABLE_TESTS=ON, ENABLE_LOCAL_BUILD_SHORTCUTS=ON,系统依赖(Qt6=/usr/lib/cmake/Qt6, OpenCV5, QCA, GDAL/PROJ/GEOS/GSL/netCDF 系统库)
- 测试:宿主 cmake 无 ctest → 直跑 Catch2 二进制 -j1,QT_QPA_PLATFORM=offscreen;PROJ_DATA 按需注入

## 域测试人口(tests/CMakeLists.txt 实拉,68 目标)

test_e2e_open_issues test_experiment_studio_dock test_faultlab test_faultlab_params_r8
test_grader_{adapters,engine,render,schema} test_guided_lab_workspace_r8 test_harness_lab_evals
test_harness_lab_injection test_lab_{batch,batch_classroom_safety,batch_v2,chains,data_pack,
document,grader_kernels,grading,offline_e2e,report,report_cli,runtime,scale,self_check,
session_store_r16_c_t05,source} test_labspec test_output_verifier test_r16_a_faultlab
test_r16_a_grader test_r18_e_grader_faultlab test_r18_e_verifier_rules
test_sample_label_annotation test_science_verification_10 test_scientific_planner_teaching
test_study_e2e test_suitability_labels test_suitability_teaching test_teacher_credential
test_teaching_admin_dock_smoke test_teaching_cockpit_{authority_r4,smoke}
test_teaching_credential_paths_r4 test_teaching_fake_grader_cli test_teaching_foundation_e2e
test_teaching_restore_r16_c_t05 test_verification_{env_12,failure_11,metamorphic_11,
numeric_reference_11} test_verifier_{adversarial,engine,levels,numeric,packs,provenance,
render,robustness_r4,schema} test_verify_adapters{,_grid} test_verify_chain_locale_matrix
(+ test_teaching_admin_core / test_teaching_authoring_failures_r4 / test_teaching_admin_rubric_weights_r16_c_t06 经 sicnu_add_teaching_admin_test 注册)
