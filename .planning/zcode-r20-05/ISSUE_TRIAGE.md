# R20-05 ISSUE_TRIAGE — 逐条 issue 记账(LIVE master `28e1891390`)

格式:`(issue编号, 实际路径/行, master blob/SHA 结论, 已合并 PR, 是否仍能 RED, 验证, 处置)`

## 仍开放 → 本轨修复(RED 可造)

| Issue | 路径/行(实测 master) | 现状结论 | 处置 |
|---|---|---|---|
| **#1652-1** | `src/teaching_admin/rubric_builder.cpp:165-247` validateProcessRubric | 校验扁平 `criteria[]`+weightΣ1.0+metricKey/answerKey;引擎真实 schema 是 `dimensions[].criteria[]`+maxPoints(grader_types.cpp:1052-1146, :630 weight==ΣmaxPoints, :641 Σweight==totalPoints)。合法引擎 rubric 被管理端判非法。路由:teaching_admin_dock.cpp:313-316 + release_preflight.cpp:69-74(发布门禁) | **修复**:委托 `GradingRubric::fromJson`(grader 叶库,零 Qt)——ONE parser no second truth(与 lab_pack 委托同款);更新 pin 旧行为的 3 个测试 |
| **#1600** | `src/verify/verify_engine.cpp:440-481` + `src/verify_adapters/gdal_grid_probe.cpp:73-116` | nodataFraction/finiteFraction 由 ≤9 窗采样算出,verbatim 写入 evidence 当作整幅事实;`hasSampling/sampledPoints`(verify_types.h:124-125,序列化+roundtrip)全仓无生产者 | **修复**:GridInfo 增采样计数;evalArtifactGrid 证据盖采样章 |
| **#1607(残余)** | `src/teaching/lab_completion_ledger.cpp:156-160`、`src/teaching/verdict_stamp.cpp:130-135` | 默认 CharReaderBuilder(无 stackLimit)且无 try/catch;主站点 lab_session_state.cpp:224-251 已修(#1664),两处同类站点未修 | **修复**:pin 256 + typed catch(fail-closed 为 parse issue) |
| **CT-1 merge-loss**(R19-02 报告,非 issue) | 见下 WP-ML | merge `c8f5e0805422`(r16-b 并 #1517 r15-E)丢弃教学侧修复,至今未归 | **修复**(WP-ML,4 项) |

## WP-ML:merge c8f5e0805422 丢失清单(本轨区,实测 == merge-base)

| 路径 | 丢失内容(side2 vs base) | 今天 master | 恢复方式 |
|---|---|---|---|
| `src/teaching/course_home_view_model.cpp` | overallPercent 钳制 [0,100] + 从 completed-lab 集合按 labId 去重派生展示百分比 | **==base(line 137 裸 cast)** | verbatim 语义恢复 |
| `src/teaching/lab_feedback_projection.cpp:290` | 证据缺/非布尔 `passed` → `indeterminate`(不再把从未评过的断言判成 fail,聚合翻整实验未通过) | **语义仍缺**(三元的 else 分支直接 "fail") | 语义恢复 |
| `src/teaching_admin/batch_assessment.{h,cpp}` | `loadBatchReportFromFile` fail-closed 加载器(截断/无关 JSON 不再变成「0 份作业」) | **函数不存在**(grep 全仓无) | 语义恢复,适配 #1664 后结构 |
| `src/app/teaching_admin/teaching_admin_dock.cpp:551-640` | finishBatchOnUiThread/onBuildFeedbackAndSummary 两处改用 fail-closed 加载器 | **==base(裸 toInt() 在 :558/:560/:618/:621)** | 语义恢复 |
| `tests/test_r15_E_teaching_stats_honesty.cpp`(247 行,6 case) | 钉 WP-ML 前两项 | 文件被 merge 整体删除 | 恢复(适配现 schema) |
| `tests/test_r15_E_batch_refusal_accounting.cpp`(360 行,9 case) | 钉 discoverSubmissions 记账 + fail-closed 加载器 | 文件被 merge 整体删除;**两半都未回归**:0a8f56a28d 只记账保留输出名,808f1c4c52 显式维持 hidden/nested 不可见(R1 评审 P0 纠正了初版"discovery 半区已修"的误判;disc_driver 实证 base 0 skips) | 两半同时恢复(生产记账块 + 测试),驱动 base-RED/HEAD-GREEN 见 RUN_LOG |

## 已在 master 修复(本轨只做保护性验证/建议关闭,不重复修)

| Issue | 证据(master 实读) | 修复来源 |
|---|---|---|
| #1558(P1 恒真断言) | test_e2e_open_issues.cpp #784/#799 两用例已重写为真断言(逐字段比对/终态轮询) | #1662/#1664 后续 |
| #1598 | render_teaching.cpp derivedStatus 折叠 uncovered results(:277-292)+ contributingStatus 尊重 promotion | #1664(r18-e) |
| #1599 | packs_builtin.cpp 已写评估器真实词表(expect.kind/required_dimensions/expected.algorithm,注释明示) | #1664 |
| #1602 | bounded_io.cpp:50-80 按文件大小分配 + cap 钳制 | #1664 |
| #1593 | lab_pack.cpp:242-262 stackLimit=128 + try/catch | #1664 |
| #1610 | fault_expectations.cpp:50-74 delta 关系基线缺失 fail-closed | #1664 |
| #1611 | fault_observables.cpp:144-145 numerator/denominator isString 门 | #1664 |
| #1612 | fault_observables.cpp:277-298 按原下标配对,非数值对丢弃 | #1664 |
| #1613 | fault_scenario.cpp:426-429 拒绝空 verifier 声明(typed error) | #1664 |
| #1607 主站点 | lab_session_state.cpp:234 stackLimit=256 + catch | #1664 |
| #1652-2 | feedback_pack.cpp:62-64 含 `m == "student_"+studentId` | #1664 后续 |
| #1653 | checks_{provenance,reproducibility,cross_output} 均调用导出的 beginResult(checks_numeric.h:69) | #1664 后续 |

## 跨轨(本轨禁写,仅 CROSS_TRACK_FINDINGS 记录)

| Issue/线索 | 路径 | 归属 |
|---|---|---|
| #1536(P1) | src/app/widgets/guided_workflow_widget.cpp:168(错误行计数偏移选错实验) | 04 轨(app/widgets) |
| #1601(P2) | src/verification 与 src/verify 双核;ADR 0172 归属矛盾 | 需 maintainer 架构决策;本轨仅修 src/verify 头部 ADR 归属注记 + 记录证据 |
| #1607 同类 | src/recipes/lab_document.cpp:283(钉了 256 未捕获)、src/recipes/provider_interfaces.cpp:62-67(两者皆无) | 未明确分轨(非本轨白名单)|
| R19-02 CT-1 其余 | src/app/shell/*、src/app/widgets/rs_result_summary、src/help/operator_help_provider | 03/04 轨 |
