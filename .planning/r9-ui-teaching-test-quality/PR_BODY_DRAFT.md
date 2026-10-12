# fix(ui,teaching,faultlab): close PR #1474's five unresolved review threads (R9 direction C)

Base: master @ 8a08642760e2e8ffd2c4b965fc4683c01d3e4758 ｜ Head: c8bd0a52a97962c7a9cd1fa3923b9984ce460376 ｜ **不等待线上 CI**（本地定向编译 + CTest 验证，Windows/MSVC 实机）

## 问题与修复（每项含 red→green 证据）

### T0 fault_transforms — 拒绝的 transform 不得修改输入
- **Bug**: train_test_spatial_leakage 在 duplicate/relocate 目标验证前发布 leakage_cell_size；被拒场景仍改写了观测 binning 锚点（mutations=0 却有副作用）
- **Root cause**: 发布点位于所有 mode 特定拒绝检查之前
- **Fix**: 移至两条成功路径（cloned>0 之后 / relocate 目标检查之后）（commit 7430953dc）
- **Repro/Regression**: tests/test_faultlab_params_r8.cpp "refusals leave the grid unmutated"（duplicate 无 train / relocate 缺 test 两形状，fixture 逐字节比对）
- **证据**: RED = stash 修复后 2 断言失败（8.0 == 4.0 ×2）；GREEN = 2/2 Passed ×2 轮

### T1 guided_workflow_widget — 异步作业期间 Next 仍可点
- **Bug**: showStep 在提交前评估 isRunning()，提交后 Run 禁用但 Next 保持可点，onNextStep 静默吞掉点击
- **Fix**: runOperatorStep 提交时同步禁用 Next；terminal 回调/提交拒绝路径恢复（commit c8bd0a52a）
- **Regression**: tests/test_guided_workflow_widget.cpp "Next stays disabled for the whole flight"（JobEngine prefix executor rs:slow_probe_r9 门控任务在飞）
- **证据**: RED = CHECK_FALSE(nextButton) 展开为 !true 失败；GREEN = 11/11 Passed ×2 轮（含端到端 positive control）

### T2 rs_scan_pool — 销毁 owner 的 cancel marker 被 pruning 回收后 worker 复活
- **Bug**: destroyed hook 抹掉 owner 记录后，1024 次 cancel 触发 bounded pruning 可把该 owner 的 cancel marker 一并回收；在飞 worker 轮询 isStale(gen, owner) 双双落空，恢复本应停止的栅格扫描
- **Fix**: missing owner record 即 tombstone，isStale fail-closed 返回 stale（commit 64b7901c9）
- **Regression**: 更新 #1453 用例期望 + 新增 1100 次 cancel pruning 压力用例
- **证据**: RED = 恰好 2 用例失败（其余 8 不受影响）；GREEN = 10/10 Passed ×2 轮
- **下游影响面验证**（isStale 消费方全绿）: test_histogram_widget + test_roi_statistics_teardown_r4 6/6 ×2、test_parity_state_mirror_r4 18/18 ×2、test_parity_async_late_arrival_r4 AS-1/2/3 3/3

### T3 script_adapters — Windows Store alias 未探测即采用
- **Bug**: findExecutable("python") 首个命中即返回，WindowsApps alias stub（可启动、打 Store 广告、exit 9009）挡住候选表后面的真 Python；"python" 与 "python3" 都可能是 stub
- **Fix**: interpreterStarts 探测（python -c "import re"，3s/10s 上限，同时挡 alias 与 SRE mismatch）；单轮探测路径去重；成功结果进程级缓存（失败不缓存）（commits b4bf909d9 + 911a967a6）
- **Regression**: 不可用首候选（Windows: robocopy 副本真 PE / POSIX: exit 42 脚本）必须被跳过
- **证据**: RED = 3 用例恰好失败；GREEN = 7/7 Passed ×2 轮（Windows 实机）

### T4 script_adapters — verify/drift 丢弃截断标志
- **Bug**: verifyOfflineBundle/checkLabPackDrift 只解析保留头部、丢弃 stdout/stderr truncated 标志，大班级 bundle 的不完整 findings/drift 列表被当完整结果发布
- **Fix**: outputTruncated 贯穿 BundleVerifyResult/PackDriftCheck + toJson；截断时拒绝解析 transcript（exit code 仍是 verdict；exit-2 分支同样 typed）（commits b4bf909d9 + 911a967a6）
- **Regression**: 泛洪脚本三用例（stdout>8MiB exit1 / stderr>1MiB exit2 / DRIFT 列表>8MiB）
- **证据**: RED → GREEN = 7/7 ×2 轮（与 T3 同套件）

## 与 master 的去重（C2 审计，独立 subagent 核验，报告见账本）
#1453（rs_job_runner 删除 + destroyed-hook）、#1464（grader 时间戳 + cell_size real-effect）、#1465/#1447（dedup suffix/broker typed-failure/Json typed 站点/帮助资源校验）、#1466 部分（lab_session_state 原子写 + stdout cap）、#1468 全部五子项 —— 均 **ALREADY_FIXED_IN_MASTER**，本 PR 不重做。
未闭环项（后续 wave 候选，不在本 PR）：#1466 batch_assessment discovery 静默丢弃学生文件（batch_assessment.cpp:270-297）；#1465 recipe_router.cpp:112-150 标量类型门 + agent_adapter.cpp static-init 兜底。

## Review（两轮独立）
- **Round 1**（reviewer-②，未参与实现）: APPROVE_WITH_CHANGES，无 P0/P1；P2-1 探测重复、P2-2 exit-2+trunc 覆盖 + Nit fromLocal8Bit/注释 → 已修（commit 911a967a6）
- **Round 2**（reviewer-③，delta review，未参与实现）: **APPROVE**，无 P0/P1；P2 仅 PR 文本占位（本描述已回填）

## 风险与未验证范围
- **Linux/macOS lane: NOT_VERIFIED**（无对应机器）。修复代码平台中立 + Q_OS_WIN 分支隔离；假候选测试双平台设计（robocopy/POSIX 脚本）但仅在 Windows 实测
- **pre-existing 失败披露**: test_parity_async_late_arrival_r4 AS-4/AS-5（layer auto-load 系）在 writeValidProject fixture 处失败；经 origin/master 基线头文件复现**同样失败**——与本 PR 无关（本 PR 的 isStale 语义翻转在同一二进制的 AS-1/2/3 全绿），留待 test-infra 方向处理
- 教学管理 dock 的同步执行（≤120s 阻塞）为既有设计（#1474 已披露残余），本 PR 未改
- interpreter 探测在 unpinned + 全候选不可用机器上，每次 adapter 调用最多 ~64s（4 候选 × 16s），成功即缓存；dock 后台化属方向 C 长期 WP4

## 文件清单（9 源/测试文件 + 本方向规划文件）
- src/faultlab/fault_transforms.cpp
- src/app/widgets/guided_workflow_widget.cpp
- src/app/widgets/rs_scan_pool.h
- src/teaching_admin/script_adapters.{h,cpp}
- tests/test_faultlab_params_r8.cpp, tests/test_guided_workflow_widget.cpp, tests/test_scan_pool.cpp, tests/test_teaching_admin_script_adapters_r8.cpp
- .planning/r9-ui-teaching-test-quality/*（跟踪文件，git add -f 提交）

## 已覆盖 review threads / Issue
#1474 T0–T4（五条 unresolved review threads 全部闭合）；关联 #1453（tombstone 语义收尾）、#1464（fail-before-mutate 补强）、#1466（alias + truncation 收尾）

**未等待线上 CI。**
