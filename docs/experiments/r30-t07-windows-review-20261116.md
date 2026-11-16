# R30-T07 Windows 集成复核（2026-11-16）

本记录对应 PR #2061 的代码提交 `58030ea06cc3f2be0553e12fe7a8e6feb9dff63a`，集成基线为 master `2c58c0e62928faae230eb25cd80fd758da11d202`（含 PR #2071）。后续用户指南与本记录的提交仅修改文档。初始 Linux head `35f7a622` 的历史数字保留在 [原验证记录](r30-t07-verification-and-review.md)，不能替代本轮结果。

## 修复的交付边界

- Studio 将最近成功提交与 `pending_submission` 分开保存；B 运行失败不会用 B 设计导出 A 报告。恢复未完成提交不会自动重放，也不会伪装为成功或永久忙碌。
- 提交身份同时校验 spec、canonical digest 和存在时的 VM draft spec；直接报告匹配和 JSON 恢复使用同一自洽性检查，保留兼容的旧字段缺省。
- Matrix 按同一报告冻结完整 metric/protocol/page/cells 投影；尚未应用的控件改动不会把旧报告数值标成新指标。
- 教师反馈固定已加载报告的 lab/course/source；从同一份有界读取的报告字节计算预期 digest。源文件改变时在创建交付目录前拒绝，失败重建清除旧包缓存。报告 digest 不冒充配置 digest。
- 未改变的手工草稿保留原规则/产物来源；同一 lab 的刷新保留内存修改，跨 lab/course/store 正确隔离。保存失败时 workspace 和 cockpit 都拒绝切换，保留原正文、会话及产物。
- stale 状态独立于翻译文案；新增 90 条中文翻译，补齐本轮按钮、提示和交付反馈。

## 构建与执行

Windows MSVC 2022 / Qt 6.8，`build-dev`。所有本项目 native 构建与测试由主 agent 串行调度；构建使用 `scripts/dev/resource_guard.py --lock-dir build-dev`，显式 `cmake --build build-dev --parallel 2`，GUI 测试使用 `QT_QPA_PLATFORM=offscreen`。未终止其他项目的构建。

24 个目标完整重建成功；最后仅修改教师测试桩后重建 helper、教师 core 和 dock smoke，退出码均为 0。运行入口为本地日志目录内的 `pr2061-build.cmd`、`pr2061-gated-run.ps1`、`pr2061-teacher-build.cmd`，环境设置由 `build-logs/run_gui_delivery_tests.ps1` 提供。

| 验证 | 用例 | 断言 | 结果 |
|---|---:|---:|---|
| `test_r30_t07_core` | 18 | 326 | PASS |
| `test_r30_t07_gui` | 12 | 391 | PASS |
| `test_r29_t04_step_sync` | 8 | 112 | PASS，包含实际英文/中文 QM |
| `test_teaching_admin_dock_smoke` | 6 | 146 | PASS，包含报告来源变更与旧反馈拒绝 |
| `test_r29_t04_completion_ledger_loop` | 3 | 64 | PASS，包含 cockpit 保存失败不换 lab |
| `test_teaching_admin_core` | 36 执行，1 跳过 | 3,436 | PASS；真实外部评分器 CLI 为显式 opt-in，未提供，未执行 |
| 其余 18 个关联目标 | 129 | 6,750 | PASS |
| **合计 24 个目标** | **212 执行，1 跳过** | **11,225** | **全部执行的断言通过；所有目标退出码 0** |

其余目标：`test_r29_t04_studio_spec_unification`、`test_r29_t04_studio_session_and_import`、`test_r29_t04_history_actions`、`test_r29_t04_run_comparison`、`test_experiment_studio_core`、`test_study_spec`、`test_study_runner`、`test_study_analysis`、`test_study_spatial`、`test_study_export`、`test_r18_e_studio_session`、`test_r18_e_studio_projections`、`test_r22_teaching_batch_regrade`、`test_r22_teaching_guard_hygiene`、`test_teaching_restore_r16_c_t05`、`test_studio_live_e2e`、`test_experiment_consistency_r4`、`test_experiment_txn_r4`。

翻译 freshness gate：`unfinished=0`、新增未登记消息 `0`，PASS。保留 5 个历史 stale source 条目并输出警告，未删除旧译文。`git diff --check` 通过。

## 真实失败与复验

1. 初始身份/草稿/反馈回归：7 次用例执行中 44 条断言失败；保存失败切换回归：3 次用例执行中 18 条失败；提交内部自洽性与 Matrix 投影回归：3 次用例执行中 48 条失败。以上均是成功构建、匹配到测试后的行为失败，分别保留 RED 日志；不是独立用例总数，也未把构建错误计作 RED。
2. Windows 测试可移植性：移除 POSIX-only PID/temp 假设；Qt-free restore 测试使用 `portable::pid()` 与 UTF-8 filesystem 转换，保留原 Unicode roundtrip 和防伪断言。
3. 教师 CLI fixture 使用原生 helper 运行真实进程；Windows 使用宽字符 argv、完整 CRT quoting 和 UTF-16 环境变量，覆盖空参数、空格、引号、反斜杠及含空格/中文的仓库路径。保留真实子进程异常退出、超时/取消、8..10 已评分和至少 10 取消、CSV/分数等原校验。
4. 完整教师 suite 曾因两个并发 grader 回调中的 Catch 断言触发 `redirect is already active` 崩溃；改为原子收集打开错误，评分线程 join 后由主线程断言，保留默认双线程和取消/原子发布/regrade 检查。修复后完整 37 个用例发现、36 个执行通过，1 个 opt-in 跳过。

日志位于 gitignored `build-logs/pr-all-20261116/`：`pr2061-final-build.log`、`pr2061-teacher-worker-fix-build.log`、`pr2061-test-summary.json`、各目标日志、`test_teaching_admin_core-final.log`、`test_teaching_admin_dock_smoke-final.log`、`pr2061-pin-final.log`、`pr2061-translation-final.log`，以及 `*-red.log`。汇总中的教师 core 指向最后完整复验日志；没有用前一版 pin 失败结果冒充通过。

## 独立审查与验证范围

Standards 与 Spec 两条审查分别复核并绑定代码 SHA `58030ea06cc3f2be0553e12fe7a8e6feb9dff63a`；修复过的提交身份、反馈来源、保存拒绝和 Matrix 投影问题没有回退。最终结论记录于 PR。

本轮 Qt UI 验证采用真实控件的 offscreen 测试；没有关闭、保存或重启用户保留的未保存应用窗口。未运行真机渲染、GPU、多平台 sanitizer 或外部真实评分器 CLI；没有声称本轮重新生成了 Windows 发布包。

Python/NDVI 的继承基线另属 PR #2071：其代码 `b3b8adf1` 的随包 Python 3.12.16 MVP 曾完成 9 个用例、108,794 条断言，包括离线 NDVI、结果资产/来源、失败和取消。本轮 #2061 未改 Python 执行环境，不把这份基线结果计入上述 24 个目标。

## 远端 CI 与合并依据

用户已明确授权以通过的本地验证为合并依据，并注明远端待跑。远端状态不等同于本地结果。

记录时，[PR #2071 的最新 CI](https://github.com/WindWang2/exp-rs/actions/runs/43856540845) 仍未全绿：Linux Fast 编译和 clang-tidy 通过，[CTest 原文](https://github.com/WindWang2/exp-rs/actions/runs/43856540845/job/133262144193) 为 `99% tests passed, 93 tests failed out of 10212`，即 10,212 项中失败 93 项；Full Integration 链接阶段磁盘满，sanitizer 测试未执行；macOS native 和 Windows 依赖成功，Windows MSVC 仍运行。Python Windows/Ubuntu 和 Pi Windows 独立工作流成功，Python macOS 取消。未将真实测试失败泛称为基础设施问题，也未判定这些失败均由本 PR 引入。

PR #2061 推送后远端检查的即时状态以 [PR 检查页](https://github.com/WindWang2/exp-rs/pull/2061/checks) 为准；本记录不宣称远端检查全部通过。
