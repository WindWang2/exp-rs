# Goal-Loop 账本

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|----|------|------|------|--------|
| 0 | 基线审计+worktree+5 文件代码审读+review threads 获取 | git/gh 实测 | ✅ | C1-1 |
| 1 | C1-1 fault_transforms: leakage_cell_size 发布移至两条成功路径（duplicate cloned>0 后 / relocate 目标检查后）；test_faultlab_params_r8 新增 fail-before-mutate 用例（两 SECTION） | 待编译验证 | 未验证 | 编译+ctest |
| 2 | C1-2 guided_workflow_widget: runOperatorStep 提交时禁用 Next（terminal 回调恢复）；test 新增 JobEngine prefix-executor 门控用例 | 待编译验证 | 未验证 | 编译+ctest |
| 3 | C1-3 rs_scan_pool: isStale 对 missing owner record fail-closed 返回 stale；更新 #1453 用例期望 + 新增 pruning 压力用例（1100 次 cancel） | 待编译验证 | 未验证 | 编译+ctest |
| 4 | C1-4 script_adapters: interpreterStarts 探测（-c "import re"，3s/10s 上限）+ 成功缓存（失败不缓存）+ 候选逐一探测；新增 fake 不可用候选测试（Win: robocopy 副本 / POSIX: exit 42 脚本）；更新 unpinned 契约断言 | 待编译验证 | 未验证 | 编译+ctest |
| 5 | C1-5 script_adapters: BundleVerifyResult/PackDriftCheck 增 outputTruncated + toJson；verify/drift 截断时拒绝解析 transcript（含 exit-2 分支）；新增两个 >8MiB 泛洪脚本测试 | 待编译验证 | 未验证 | 编译+ctest |
| 6 | C2 去重审计（subagent-① Explore）: 18 子项判定完成；除 2 个后续候选外均 ALREADY_FIXED | 报告入 PR_ISSUE_REVIEW_MATRIX | ✅ | 编译期间执行 |
| 7 | C1-1 red: stash 修复后 test 638 失败 (8.0==4.0 ×2 SECTION)；green: #637/#638 2/2 Passed (0.95s) | ctest 实测 | ✅ | commit 7430953dc |
| 8 | C1-3 red: #520 (3p/2f) + #521 (isStale→false) 恰好失败、其余 8 不受影响；green: 10/10 Passed (1.85s) | ctest 实测 | ✅ | commit 64b7901c9 |
| 9 | C1-4/5 red: #857 探测/#860 verify 截断/#861 drift 截断恰好失败；green: 6/6 Passed (2.67s, Windows 实机含 robocopy 假候选) | ctest 实测 | ✅ | commit b4bf909d9 |
| 10 | 剩余: C1-2 heavy build (qgis_core/gui) 分块进行；之后 gww red-green | 编译中 | 进行中 | 监控内存 |
| 11 | Reviewer-② 对抗审查 4 commit+未提交 gww diff: APPROVE_WITH_CHANGES, 无 P0/P1; P2-1 探测重复/最坏冻结, P2-2 exit2+trunc 无覆盖, Nit fromLocal8Bit/注释 | 审查报告 | ✅ | 修复 P2/Nit |
| 12 | 修复: 候选路径去重(单轮每路径一次探测)、新增 exit==2+stderr 截断用例(右尺寸 1MiB+)、fromUtf8、version_mismatch 优先注释 | ctest 7/7 Passed (2.62s) | ✅ | commit (round-1 fixes) |
| 13 | gww heavy build 恢复（其他并行任务占内存，监控中） | 编译中 | 进行中 | C1-2 red-green |
| 14 | 内存 1.4GB 空闲（其他并行任务同时编译）→ 按规则降 -j1 续建 gww | 监控 | 进行中 | 内存回升可回 -j2 |
| 15 | 双轮 gate 第二跑: faultlab_params 2/2, scan_pool 10/10, script_adapters 7/7 全绿 | ctest 实测 | ✅ | 待 gww |
| 16 | gww heavy build 完成 (1659 步, -j1); C1-2 red: #458 CHECK_FALSE(nextButton) → !true 失败; green: 全 11 用例 Passed (5.65s, 含端到端 positive control) | ctest 实测 | ✅ | commit c8bd0a52a |
| 17 | C1-2 双轮第二跑: gww 11/11 Passed; 下游回归 (isStale 语义翻转影响面): histogram+roi_teardown 6/6, state_mirror 18/18, async AS-1/2/3 3/3; AS-4/AS-5 为 pre-existing 环境失败 (writeValidProject fixture, origin/master 基线头文件复现同样失败, 与本 PR 无关, 如实披露) | ctest 实测 | ✅ | reviewer-③ delta |
