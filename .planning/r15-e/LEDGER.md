# R15-E Oracle Ledger

执行循环：`分析 → RED → 最小修复 → GREEN×2 → 记账 → 下一任务`。
每行记录：工作包 / 缺陷 / RED 证据 / 修复 / GREEN 证据 / 状态。

| # | WP | 缺陷 | RED 证据 | 修复 | GREEN | 状态 |
|---|---|---|---|---|---|---|
| R15E-1 | E05 | `RsResultSummary` 把不存在的产物路径建成可点击项 | `test_r15_E_result_artifact_honesty` 用例 1（修复前：缺失项 enabled 且双击发出 openPathRequested） | `src/app/widgets/rs_result_summary.cpp` 加 `QFileInfo::exists` 判定，缺失项禁用且不写 UserRole | 用例 1 通过（23 assertions / 3 cases 全绿） | ✅ GREEN |
| R15E-2 | E05 | 状态行硬编码 `✓ Processing finished`，忽略 result 自带 status | 同一测试用例 2（修复前 status="failed" 仍渲染 ✓） | 同上：按 `status` 选择 `✗ Processing failed` + `state=error` | 用例 2/3 通过 | ✅ GREEN |
| R15E-3 | E05 | `setFailed()`/`setRunning(true)` 不清上一次运行的成功结果卡 | `test_r15_E_task_panel_state` 用例 1/2 | `task_panel_host.cpp` 新增 `clearResultSummary()`，两处调用 | 待本轮构建运行 | 🟡 已修待验 |
| R15E-4 | E05 | pipeline 终态分支从不 `setRunning(false)` → Run 永久 "Stop"、表单永久禁用、二次运行被拒 | 静态证据（`isSingleJob` 分支有、`isPipelineJob` 分支无；`setRunning(false)` 全仓仅两处调用点） | `workflow_session_controller.cpp` pipeline 终态补 `m_panel->setRunning(false)` | 静态 + `test_workflow_session_controller` 回归 | 🟡 已修待验 |
| R15E-4b | E05 | `cancelActiveRun()` 先清 id → 步骤永久停在 "running" | 静态证据 | 取消时把当前定义的步骤状态复位为 `idle` | 同上 | 🟡 已修待验 |
| R15E-5 | E05 | `formatProgress()` 对 Failed/Canceled 仍打印百分比 | `test_r15_E_task_panel_state` 用例 3 | `rs_job_panel.cpp` 终态返回 `—`；顺带把函数改为 public static 以便断言 | 待本轮 | 🟡 已修待验 |
| R15E-5b | E05 | `onClearFinishedClicked()` 只扫顶层行，pipeline 子步骤清不掉 | 静态证据 | 递归收集（子行全终态时只删父行，避免悬垂指针） | 静态 | 🟡 已修待验 |
| R15E-5c | E05 | Load 按钮只看"有路径"不看"文件存在" | 静态证据 | `updateActionEnabled()` 要求至少一条路径真实存在，并给禁用原因 tooltip | 静态 | 🟡 已修待验 |
| R15E-6 | E08 | `overall_percent` 原样照搬（105% 直接渲染 / 陈旧 0 与已完成集矛盾） | `test_r15_E_teaching_stats_honesty` 用例 1/2 | `course_home_view_model.cpp` 夹取 [0,100] 并改由完成集与课程实验数推导 | 4 cases / 23 assertions 全绿 | ✅ GREEN |
| R15E-7 | E08 | evidence 缺布尔 `passed` 被判 fail，经 aggregate 把整个实验判为未通过 | `test_r15_E_teaching_stats_honesty` 用例 3（修复前 status=="fail"） | `lab_feedback_projection.cpp` 缺省归一为 `indeterminate` | 用例 3/4 通过 | ✅ GREEN |
| R15E-8 | E09 | `lab_pack` 解析无 `stackLimit`、无字节上限、异常逃逸 | `test_r15_E_lab_pack_bounds` 用例 2（RED 实锤：`Exceeded stackLimit in readValue()` 异常逃逸出 `loadFromBytes`） | `lab_pack.cpp` 加 8 MiB 上限 + `stackLimit=64` + try/catch → typed `lab.pack_schema` | 3 cases / 8 assertions 全绿 | ✅ GREEN |
| R15E-9 | E08/E09 | 隐藏学生目录 / 嵌套目录用裸 `continue` 静默蒸发（整份作业从 rows/skips/total/checkpoint 同时消失） | `test_r15_E_batch_refusal_accounting` 用例 1–3 | `batch_assessment.cpp` 三处 `continue` 改为带原因的 `recordSkip`（根级普通文件仍交给扁平 pass，不重复记账） | 待本轮 | 🟡 已修待验 |
| R15E-10 | E08 | 损坏/半写批改报告被静默吞掉，渲染成合法的 "0 份作业 / 0% 完成" 班级汇总 | `test_r15_E_batch_refusal_accounting` 用例 4–7 | 新增库级 `loadBatchReportFromFile()`（bounded read + parse error + rows 校验 + total 不一致告警），dock 两处改用它 | 待本轮 | 🟡 已修待验 |
| R15E-11 | E10 | 帮助 `algorithm.keyParameters` 与算子 schema 漂移（3 处：`rs:pca.componentCount`、`rs:band_math.bands`、`rs:morphology.window`） | 全量比对脚本：31 个带 keyParameters 的条目 / 84 个参数名 → 3 处缺失于 `docs/generated/help/parameters.md` | 修正 3 处 JSON；`operator_help_provider.cpp` 增加 keyParameters 与 schema 参数集的一致性校验（走既有 `report()` 通道，`test_help_coverage` 即刻成为护栏） | 复验脚本 drift=0；help 测试待本轮 | 🟡 已修待验 |

## 未修 / 延后（据实记账，不伪造完成）

| 项 | 阻塞 / 理由 |
|---|---|
| `cancelActiveRun()` 不再提前清空 `m_activePipelineId`/`m_pendingTaskId` | 现有 `test_workflow_session_controller.cpp` 明确断言 `activePipelineId() == -1`；改动需同步改断言并重新论证并发语义。本轮只做了等价且无风险的步骤状态复位（R15E-4b）。 |
| `src/grader/grader_json.cpp` 缺 `stackLimit`（注释声称会捕获 "Exceeded stackLimit" 但从未设置） | **所有权越界**：`src/grader/**` 不在本轨允许写入清单。已记录证据，建议交对应轨道。 |
| `rs_job_panel.cpp` 右键 6 个动作禁用无原因；`ribbon_controller::addActionButton` 丢失禁用原因且 `accessibleDescription` 在禁用态被写成通用 tooltip；`qgsgeoref_shell_window` Run 按钮禁用零原因；14 个数字化工具禁用零原因 | 子代理给出「4 处独立实现各自漏一遍」的统一诊断（仓库已有 3 份正确实现）。收敛成公共件 + 一条源码扫描契约测试是更合适的形态，超出本轮预算，列为后续候选。 |
| `rs_job_panel` 加载产物「部分成功静默」（n>0 但 <请求数 无任何提示） | 已具备**启用态**正确性（禁用原因 tooltip + 存在性过滤），可见后果有限；列为 P3 后续。 |
| 帮助 guidance 渲染裸命令 id、`WorkbenchGuidance`/`bindWidget` 从未接线、菜单手写 tooltip 被帮助系统整体覆盖 | 子代理已给出 STATIC_CERTAIN 证据，但改动面涉及初始化顺序与内容迁移，超出本轮预算；如实列为未展开。 |
| E11 主题 / E13 无障碍 / E15 离线机房 / E16 性能画像 | 本轮算力优先给了已实证缺陷；**NOT VERIFIED**，不在报告里假称完成。 |

## 环境事实（本轨新增，写入 BASELINE）

1. 并行轨道的 `vcpkg` 进程持有 `/c/deps/vcpkg/buildtrees/vcpkg-running.lock`（`mv` 报 `Device or resource busy` 证实为 live process）→ 本轨 configure **不挂 vcpkg toolchain**。
2. `vcvars64.bat` 内部调用 `reg.exe`，被沙箱拦截 → `rc.exe`/`LIB` 半缺失；改为 `env_r15e.cmd` 显式设置 VS/SDK。
3. 全局 `C:\deps\vcpkg\installed` 早于 manifest 的 jsoncpp baseline → `find_package(jsoncpp)` 退化为裸库名；通过 `INCLUDE`/`LIB` 环境变量补齐（等价于工具链本应加的内容）。
4. `resources/icons` 是 git 符号链接（blob 内容 `../docs/design/ui/svg-icons/icons`），`core.symlinks=false` 时被检成普通文件 → `qrc_icons` 缺文件。本轨用 `ln -s` 还原，`git status` 保持干净。
5. Bash → `cmd //c` 桥接会破坏批处理环境（`'m' is not recognized`）→ 测试统一走 PowerShell runner。
