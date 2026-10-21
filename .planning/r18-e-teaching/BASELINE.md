# R18-E BASELINE — 教学实验、科学证据与可复现评价：状态真值和完整反馈闭环

- 快照任务书基线: master=7825c1ebd (2026-10-19 快照, 仅定位用)
- **实际执行基线 (LIVE)**: `origin/master = a60c8831c9cc4e64701d17bde2ace67a74a9b7a0`, fetch 于 2026-10-19T04:34+08:00
- Worktree: `/home/kevin/project/exp-rs-r18-e-teaching`, branch `agent/r18-e-teaching-evidence` @ a60c8831c
- 构建树: `build-r18-e` (Ninja, Debug, ENABLE_TESTS=ON, -j2, 全局锁 /tmp/exp-rs-five-tracks-build.lock)

## LIVE Recon 结论 (2026-10-19)

- 快照中的开放 PR **#1513 已合并** (r15-B geodata-persistence)。其原写集 `src/experiment/** src/dataset/** src/data/** src/geospatial/**` 已进入 master；本轨仍不改这些目录（非本轨范围）。
- 当前唯一开放 PR: **#1534** (r10-algorithms, 写集为 `.planning/r10-algorithms/`、`data/processing/algorithm_meta/**`、`src/analysis/**`、`src/agent/harness/capability_catalog.cpp`、`pi/knowledge/**`) — 与本轨零交集。
- Issues: **#1467** (core-fork local-modification cluster; 所有者 2026-10-18 已核验并记录撤销原因) 与 **#1491** (CI 常态化/ macOS 根因; 所有者已核验) — 均非本轨写域，仅引用。
- 姊妹轨道 worktree 在场: r18-a (science), r18-b (model-runtime), r18-c (agent)。本轨不碰它们的写域。
- `overlap_scan.py src/{teaching,teaching_admin,lab,grader,verification,experiment_studio,help}` → overlap_count=0。
- 最近合并的本域 PR: #1519 (学生会话/教师批量), #1521 (verification/grader/faultlab fail-closed), #1523 (help/i18n), #1525 (checkpoint stamp interior), #1526 (UTF-16 surrogate truncation)。这些已修内容只做回归，不重修。

## 已合并本域修复（避免重复劳动）

| PR | 修复 | 关键文件 |
|----|------|---------|
| #1519 | session envelope/identity、batch checkpoint、rubric weights、student_id authority、grader CLI budget | src/lab/session_*.cpp, src/teaching_admin/batch_assessment.cpp |
| #1521 | canonical JSON -0.0/±Inf/budgets、fail-closed kinds/providers | src/verification/*, src/faultlab/*, src/grader/grader_json.cpp |
| #1523 | help 数据一致性、搜索确定性、i18n gate | src/help/* |
| #1525 | checkpoint stamp 头/尾/内部指纹 | src/teaching_admin/batch_assessment.cpp:69-100 |
| #1526 | summary 截断不拆 UTF-16 代理对 | src/help/help_descriptor.h truncateSummary |

## 三路深读审计结论摘要（完整报告见 LOOP_LEDGER 与 commit 记录）

**Track A (teaching/teaching_admin/lab)**:
- D1 P0: 课程完成状态从未持久化 — `src/app/teaching/lab_cockpit_dock.cpp:371-373` 每次启动用 `CurriculumProgress::emptyDoc()`，`markCompleted` 无任何 app 调用者 → 先修 DAG 在发布 UI 中永远不可满足。(src/app 属 R18-D，本轨提供 teaching 侧权威契约 + 跨轨记录)
- D2 P1: CLI 批量发现跟随 symlink 逃出提交根 — `src/cli/lab_batch_runner.cpp:38` (QDir::Files) vs GUI 已加固的 batch_assessment.cpp:323-392。
- D3 P1: checkpoint 采用不绑定判定真实性 — batch_assessment.cpp:152-188 只验 schema/config digest/状态词表/stamp，行内 pass/score 无 grader transcript 绑定。
- D4 P1: artifact stamp TOCTOU — stamp 在 callable 返回后采集 (batch_assessment.cpp:643)，评分期间换文件则旧分绑新字节。
- D5 P2: rubric_builder 校验弱于 grader 权威 (weight=0/sum≠100 通过 preflight) — rubric_builder.cpp:104-128 vs output_verifier.cpp:668-692。
- D6 P2: `lab_status` 恢复无权威校验 — lab_session_state.cpp:142-147。
- D7 P2: 恢复的 last_validation_summary 接受整体伪造 pass（无摘要绑定）— lab_feedback_projection.cpp:337-357。
- D8 P2: lab session `last_seq` 可抬升伪造 gate pass — session_state.cpp:781-800。
- D9 P3: `lab --grade --out` 非原子写; dock 重解析丢字段; 隐藏条目无 skip 行; GUI 无 duplicate_of。

**Track B (verification/grader/faultlab)**:
- V1 P1: grader AtMost+linearWindow 部分分死代码 — grader_matcher.cpp:467-472 要求 from>to，而 grader_types.cpp:229 只允许 to>from → 每个可解析的 AtMost 窗口 rubric 都给 0 分。
- V2 P1: 重复 check id 在 rollUp 时首匹配掩盖 Fail — report.cpp:33-44 + check_runner.cpp 无去重拒绝。
- V3 P1: faultlab 数值期望缺 clean 基线时伪造 delta=0.0 空洞通过 — fault_expectations.cpp:34（#1521 只修了 Changed/Text/Truth）。
- V4 P1: 派生契约 pack 参数与求值器词表脱节，永远 SPEC_INVALID — packs_builtin.cpp:187-222。
- V5 P2: render_teaching derivedStatus 丢弃 uncovered fold/budget 降级，可与 report.status 矛盾 — render_teaching.cpp:235-261。
- V6 P2: faultlab `expected.verifier` 块加载但从不执行 — fault_scenario.cpp:384-391。
- V7 P2: relational 无 name 指 operand 回退 subject.id 可自比恒 Pass — checks_relational.cpp:233-239。
- V8 P2: stage 期望静默默认 expectedState="Completed" — grader_types.cpp:172-174。
- V9 P3: specDigest 比较器忽略 params，(id,kind,title) 并列时组合顺序可变 digest — spec.cpp:267-283。
- V10 P3: grid_shift(0,0) 接受为已施加故障; 诊断规则在缺基线时伪造签名 — fault_transforms.cpp:235-252, fault_diagnosis.cpp:58-76。

**Track C (experiment_studio/help)**:
- H1 P2: severity 词表拼写错误静默降级为 Error（fail-open）— help_content_store.cpp:37-46。
- H2 P2: studio CSV 公式注入防护只看首字符，前导空格绕过 — studio_export.cpp:92-101。
- H3 P2: StudioSessionState::fromJson 不校验 schema — studio_session.cpp:37-66。
- H4 P3: HelpCompact::toJsonText 预算不足时输出非法 JSON — help_presenter.cpp:158。
- H5 P3: elide()/toJsonText 仍用裸 left() 可拆代理对（#1526 只修了 truncateSummary）— help_presenter.cpp:17-22,150。
- H6 P3: sensitivity spatialSummaries.first() 与 selectedPointId 错位关联 — sensitivity_projection.cpp:108-109。
- H7 P3: spec echo 缺 seed_replicates 时误报 uncertainty_missing_replicate — sensitivity_projection.cpp:87-95。
- H8 P3: run-matrix issues 不达 dock UI（src/app → 跨轨记录）— experiment_studio_dock.cpp:433-475。

## 基线机器

- Linux cachyos 7.2.9-1, 40 核 (构建限 -j2), 62GB RAM (~50GB 可用), gcc/clang 在场, ninja 在场, 无 ccache。
- Qt offscreen: QT_QPA_PLATFORM=offscreen。
