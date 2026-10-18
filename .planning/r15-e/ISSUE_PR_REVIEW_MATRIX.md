# R15-E Issue / PR / Review 矩阵（随证据更新）

分类词汇：`FIXED_ON_MASTER` · `COVERED_BY_OPEN_PR` · `REPRODUCED_OPEN`（本轨修） ·
`NOT_REPRODUCIBLE` · `OUT_OF_SCOPE` · `DEFERRED_WITH_EVIDENCE`

---

## A. 任务书指定线索 Issues

| Issue | 主题 | 归类 | 证据 |
|---|---|---|---|
| #1489 | i18n：`lupdate` 只扫 src/app，zh_CN.ts 缺大量 tr() | `COVERED_BY_MERGED_PR` + 残留审计 | #1512 已改 `resources/translations/sicnu_zh_CN.ts`、`test_i18n.cpp`、`test_i18n_migrated_translations.cpp`。本轨**不预先改锁定的 .ts**（E12 规定先审计），仅在运行期复核。 |
| #1466 | 教学脚本路径 / grader Windows python fallback / discovery 启发式 / lab session 非原子写 / 子进程无界输出 | `FIXED_ON_MASTER` | 报告已确认修复；本轨对 `src/teaching`、`src/lab`、`src/lab_pack` 另做独立审计（见 C 节），未发现同类残留。 |
| #1465 | help terminology / `std::equal` 越界 / 去重后缀未应用 | `FIXED_ON_MASTER` | 报告已确认修复；帮助系统审计见 D 节。 |
| #1482 | review：大表 / 编辑反馈 / 原子写 | `FIXED_ON_MASTER` | 由 #1510（矩阵表 bounded）+ #1512（原子写 / 矢量编辑诚实性）承接并已合并。 |
| #1494 | 孤儿测试 `test_challenger_m1_2_harness.cpp` 未注册 | `OUT_OF_SCOPE` | 与 UI/教学/帮助无关；且 `tests/CMakeLists.txt` 由 #1513 占用，本轨只在短租内追加自己的行。 |

## B. 本轨工作包 → 结论

| WP | 结论 | 说明 |
|---|---|---|
| E01 桌面任务旅程普查 | 部分 | 通过 E05 审计覆盖（任务面板/结果卡片）；完整按钮矩阵列为未完成的广度工作 |
| E02 Qt/QGIS 生命周期 | `NOT_REPRODUCIBLE`（抽样） | 抽查 `main_window_docks.cpp`/`shell/*` 的 dock 构造与 `deleteLater` 用法未发现 UAF；未做全量 |
| E03 图层树多视图一致性 | 未展开 | #1512 已合并相关图层/MapSpec 修改；本轨不重复 |
| E04 编辑工具可用时机 | 未展开 | #1512 已合并 `main_window_vector.cpp` 等；不重复 |
| E05 异步任务面板真实性 | **REPRODUCED_OPEN → 已修 5 项** | 见 C 节 R15E-1..5 |
| E06 属性表/ROI/谱图性能 | `FIXED_ON_MASTER` | #1510 bounded 500 行 |
| E07 Experiment Studio 大表 | `FIXED_ON_MASTER` | `kMaxMatrixDisplayRows=500`，统计来自完整 report 而非屏幕行；**不重写 QAbstractTableModel** |
| E08 教学闭环 | **REPRODUCED_OPEN → 已修 2 项** | 见 C 节 R15E-6..7 |
| E09 课堂异常恢复 | **REPRODUCED_OPEN → 已修 1 项** | 见 C 节 R15E-8 |
| E10 F1/控件级帮助覆盖 | 见 D 节 | 子代理审计 |
| E11 亮暗主题一致性 | 未展开 | #1512 已加 `test_theme_selector_parity.cpp`；不重复 |
| E12 中英文一致性 | 仅审计 | 遵守任务书：不提前改锁定 .ts |
| E13 无障碍/键盘 | 未展开 | 广度工作，本轮未覆盖 |
| E14 导出/持久化失败反馈 | 部分 | 由 R15E-1（产物存在性）与 R15E-2（失败不显示成功）覆盖 UI 侧 |
| E15 离线机房 Windows | 未展开（NOT VERIFIED） | 需要实机/离线包验证，本轮无 Windows 离线环境证据 |
| E16 UI 性能画像 | 未展开 | 需要基线采集；本轮未做，据实列为 NOT VERIFIED |
| E17 真实用户任务回归 | 部分 | 已跑相关既有测试目标（见 TEST_RESULTS.md） |

## C. 本轨修复清单（REPRODUCED_OPEN → 修复 + RED→GREEN）

| ID | 文件 | 缺陷 | 级别 | 测试 |
|---|---|---|---|---|
| R15E-1 | `src/app/widgets/rs_result_summary.cpp` | 产物路径未做 `QFileInfo::exists()` 校验即建成可点击项，双击把不存在的路径送进 `loadRasterLayer` | P2（E05 oracle 明列） | `test_r15_E_result_artifact_honesty` |
| R15E-2 | 同上 | 状态行硬编码 `✓ Processing finished`，忽略 result 自带的 `status` | P2 | 同上 |
| R15E-3 | `src/app/shell/task_panel_host.cpp` | `setFailed()` / `setRunning(true)` 不清上一次运行的成功结果卡 → 绿色成功与红色失败同屏 | P1 | `test_r15_E_task_panel_state` |
| R15E-4 | `src/app/shell/workflow_session_controller.cpp` | pipeline 终态分支只 `setFailed/setSuccess`，从不 `setRunning(false)` → Run 按钮永久停在 "Stop"，表单永久禁用，二次运行被 `m_runInFlight` 拒绝 | **P1** | 静态证据 STATIC_CERTAIN（见下） |
| R15E-4b | 同上 | `cancelActiveRun()` 先清空 id，终态回调再也匹配不上 → DAG 步骤永久停在 "running" | P2 | 已修（取消时复位步骤状态为 idle） |
| R15E-5 | `src/app/shell/rs_job_panel.cpp` | `formatProgress()` 对 Failed/Canceled 仍打印百分比（"Failed \| 50%"） | P2 | `test_r15_E_task_panel_state` |
| R15E-5b | 同上 | `onClearFinishedClicked()` 只扫顶层行 → pipeline 子步骤清不掉 | P2 | 静态证据（需 TaskCenter 真实嵌套） |
| R15E-5c | 同上 | Load 按钮只看"有路径"不看"文件存在" → 点了必然无效 | P3 | 同上 |
| R15E-6 | `src/teaching/course_home_view_model.cpp` | `overall_percent` 原样照搬（105% 直接渲染）、缺失时为 0、与实际完成集不一致 | P2 | `test_r15_E_teaching_stats_honesty` |
| R15E-7 | `src/teaching/lab_feedback_projection.cpp` | evidence 项无布尔 `passed` 时被判 "fail"，经 `aggregate()` 把整个实验判为未通过 | **P1（误判学生成绩）** | `test_r15_E_teaching_stats_honesty` |
| R15E-8 | `src/lab_pack/lab_pack.cpp` | `loadFromBytes()` 未设 `stackLimit`、无字节上限 | P2 | `test_r15_E_lab_pack_bounds` |

### R15E-4 的证据链（为何只有静态证据）
`onTaskUpdated()` 的终态分支：
```
if (isSingleJob) { m_pendingTaskId=-1; m_runInFlight=false; m_panel->setRunning(false); }   // 单任务有
...
if (isPipelineJob) { ...; m_runInFlight=false; m_activePipelineId=-1; m_panel->setFailed/setSuccess(...); }  // pipeline 无 setRunning(false)
```
`runFullWorkflow()` 只负责 `setRunning(true)`。因此一次 pipeline 运行成功/失败结束后，
`TaskPanelHost::m_running` 永远是 true。运行期复现需要把一条真实 pipeline 跑到终态
（依赖算子执行器与事件循环），在本轨单元测试中不稳定，故按任务书
"不适合 RED 的工作包给出有理由的证据而不是伪造 RED" 处理。

## D. 帮助系统审计（E10）
由独立 Explore 子代理执行；结论落地后补记本文件。

## E. 未修 / 延后（据实列出）

| 项 | 阻塞 / 理由 |
|---|---|
| `WorkflowSessionController::cancelActiveRun()` 提前清空 `m_activePipelineId`/`m_pendingTaskId` 导致取消无终态反馈 | 现有 `test_workflow_session_controller.cpp` 明确断言 `activePipelineId()==-1`，改动需同步修改该断言并重新论证并发语义；本轮改为"取消时立即复位步骤状态"这一等价且无风险的子修复（R15E-4b）。完整改动列为后续候选。 |
| `src/grader/grader_json.cpp` 缺 `stackLimit` | **所有权不明**：`src/grader/**` 不在本轨允许写入清单内；已记录，建议由对应轨道处理。 |
| E11/E13/E15/E16 广度工作包 | 本轮算力与上下文预算优先给了已实证缺陷；据实列为未展开，不在报告中假称完成。 |
