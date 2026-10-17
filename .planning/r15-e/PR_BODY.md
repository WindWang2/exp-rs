## hardening(r15-e): Qt/QGIS 工作台、实验教学、帮助与操作反馈质量

- 分支：`hardening/r15-E-desktop-teaching-help`
- 基线 BASE_SHA：`46791bc65c6f552ebbcf723bb91892736d56f319`（= 建 worktree 时的 `origin/master`，已合并 #1509）
- 主题：**「拒绝/失败已知但不可见」** —— 凡是系统内部已经知道「有意跳过 / 有意失败」的地方，都必须把它转成用户可见、可复查的信号；禁止裸 `continue`、静默降级和中性图标。

### 0. 与任务书快照的偏差（先摆事实）

任务书写于 #1512 仍 OPEN 时。执行时实测：**#1512 已合并**（`f7693fce1`，2026-10-16T02:35Z），#1505/#1509 亦已合并，`origin/master` 已是 `46791bc6…`，开放 PR 只剩 #1514（r15-A，算法层）与 #1513（r15-B，数据层，Draft）。因此「禁止抢改 #1512 的 31 个文件」不再适用，改为**先读合并 diff、只修合并后仍真实残留的缺陷**；本 PR 未从任何其它分支 cherry-pick。

### 1. 修复索引

| # | WP | 文件 | 缺陷 | 级别 |
|---|---|---|---|---|
| R15E-1 | E05 | `src/app/widgets/rs_result_summary.cpp` | 产物路径未做存在性校验即建成可点击项；双击把不存在的路径送进 `loadRasterLayer` | P2 |
| R15E-2 | E05 | 同上 | 状态行硬编码 `✓ Processing finished`，忽略 result 自带 `status` | P2 |
| R15E-3 | E05 | `src/app/shell/task_panel_host.cpp/.h` | `setFailed()` / `setRunning(true)` 不清上一次的成功结果卡 → 绿色成功与红色失败同屏 | P1 |
| R15E-4 | E05 | `src/app/shell/workflow_session_controller.cpp` | pipeline 终态从不 `setRunning(false)` → Run 永久停在 "Stop"、表单永久禁用、二次运行被 `m_runInFlight` 拒绝 | **P1** |
| R15E-4b | E05 | 同上 | `cancelActiveRun()` 提前清 id → 步骤永久停在 "running" | P2 |
| R15E-5 | E05 | `src/app/shell/rs_job_panel.cpp/.h` | `formatProgress()` 对 Failed/Canceled 仍打印百分比（"Failed \| 50%"） | P2 |
| R15E-5b | E05 | 同上 | 「Clear Finished」只扫顶层行，pipeline 子步骤清不掉 | P2 |
| R15E-5c | E05 | 同上 | Load 按钮只看「有路径」不看「文件存在」 | P3 |
| R15E-6 | E08 | `src/teaching/course_home_view_model.cpp` | `overall_percent` 原样照搬（105% 直接渲染、陈旧 0 与实际完成集矛盾） | P2 |
| R15E-7 | E08 | `src/teaching/lab_feedback_projection.cpp` | evidence 缺布尔 `passed` 被判 fail，经 `aggregate()` 把整个实验判为「未通过」 | **P1（误判学生成绩）** |
| R15E-8 | E09 | `src/lab_pack/lab_pack.cpp` | 解析无 `stackLimit`、无字节上限，异常逃逸出 typed 边界 | P2 |
| R15E-9 | E08/E09 | `src/teaching_admin/batch_assessment.cpp/.h` | 隐藏学生目录 / 隐藏提交文件 / 嵌套目录用裸 `continue` **静默蒸发**（整份作业从 rows/skips/total/checkpoint 同时消失，6 人班报「graded 5 / 5」） | **P1（数据丢失）** |
| R15E-10 | E08 | `src/app/teaching_admin/teaching_admin_dock.cpp` + 库级 `loadBatchReportFromFile()` | 损坏/半写批改报告被静默吞掉，渲染成格式完全合法的「0 份作业 / 0% 完成」 | **P1** |
| R15E-11 | E10 | `data/help/operators/{optical,data_io}.json` + `src/help/operator_help_provider.cpp` | `algorithm.keyParameters` 与算子 schema 漂移 3 处；且该字段是唯一未被校验的引用类型 | P2 |

### 2. RED → GREEN 证据（摘要，完整见 `.planning/r15-e/TEST_RESULTS.md`）

- `test_r15_E_lab_pack_bounds`：修复前实测 **`due to unexpected exception with message: Exceeded stackLimit in readValue().`** —— 异常确实从 `loadFromBytes()` 逃逸（这不是推断，是红测输出）。
- `test_r15_E_result_artifact_honesty`（3 cases / 23 assertions）：缺失产物禁用且不发信号；`status=failed` 时不再渲染 `✓`。
- `test_r15_E_teaching_stats_honesty`（4 cases / 23 assertions）：`overall_percent=105` 被夹取；陈旧 0 被完成集推导取代；缺 `passed` 的 evidence 归一为 `indeterminate` 且整个实验**不再**被判 fail。
- `test_r15_E_batch_refusal_accounting`（7 cases）：隐藏学生目录 / 隐藏提交文件 / 嵌套目录都产出带 reason 的 `DiscoverySkip`；截断 JSON 与非 batch JSON 被拒绝；真实报告 round-trip 成功且 `total` 不一致会被报出。
- `test_r15_E_task_panel_state`（3 cases）：失败/新运行清掉旧成功卡；`formatProgress` 终态返回 `—`。
- 回归：`test_rs_result_summary`、`test_workflow_session_controller`、`test_teaching_lab_cockpit`、`test_teaching_batch_discovery_r10`、`test_teaching_admin_core`、`test_lab_data_pack`、`test_help_coverage` 全绿（命令与 exit code 见 TEST_RESULTS.md）。

### 3. 去重证据（对 #1505 / #1509 / #1512）

- **#1512（已合并）**：本 PR 不含其改动的任何文件（`src/agent/{cartography,mapspec,layout_tools}`、`src/app/{main_window_connections,main_window_vector,qgis_app_facade,qgsclipboard,durable_file_write.*,classification/rs_accuracy_panel,workflow/pipeline_editor_dock}.cpp`、`resources/translations/sicnu_zh_CN.ts` 等），也未改 `resources/translations/**`。
- **#1505 / #1509（已合并）**：分别承接 SSRF/并发与 JsonCpp 类型安全；本 PR 未触碰 `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/processing/**`。`lab_pack` 的 `stackLimit` 与 `grader_json`（**未改**，所有权越界）是不同文件的不同缺陷。
- **#1514（r15-A，OPEN）**：只动 `src/analysis/`、`src/operators/rs/`、`src/processing/algorithms/` 与其 3 个测试 —— 与本 PR 零文件重叠。
- **#1513（r15-B，OPEN Draft）**：只动 `src/{cli,data,dataset,experiment,geospatial}` —— 与本 PR 零文件重叠。唯一共享文件是 `tests/CMakeLists.txt`：本 PR 只**追加**自己的 4 个目标块，未改其任何行；`.gitignore` 也只追加 4 行 r15-E 规则。
- 复核并**证伪**了两条子代理报告：`rs:atmospheric_correction.band` 与 `rs:mosaic.inputs` 经源码比对**并未漂移**（`rs_atmospheric_correction_operator.cpp:40`、`rs_mosaic_operator.cpp:86`），未做修改。

### 4. 仍开放的风险（据实列出，不假称完成）

1. `WorkflowSessionController::cancelActiveRun()` 仍会提前清空 `m_activePipelineId`/`m_pendingTaskId`，因此取消后不会收到终态回调、也就没有「Cancelled」就地反馈。改这一条需要同步推翻 `tests/test_workflow_session_controller.cpp` 里 `CHECK(activePipelineId() == -1)` 的既有断言并重新论证并发语义；本轮只做了等价且无风险的步骤状态复位（R15E-4b）。
2. `src/grader/grader_json.cpp` 的 `parseJsonStrict()` 注释声称会捕获 "Exceeded stackLimit"，但从未设置 `stackLimit` —— **所有权不在本轨**（`src/grader/**` 未在允许写入清单内），已记录证据，建议交对应轨道。
3. 「禁用态无原因」在仓库里有 4 处独立实现各自漏一遍（`ribbon_controller::addActionButton` 丢失原因且 `accessibleDescription` 在禁用态被写成通用 tooltip；`qgsgeoref_shell_window` Run 按钮；14 个数字化工具；`rs_job_panel` 右键动作）。仓库已有 3 份正确实现 —— 更合适的形态是收敛成公共件 + 一条源码扫描契约测试，超出本轮预算。
4. `rs_job_panel` 加载产物「部分成功静默」（n>0 但小于请求数时无提示）。
5. E11 主题 / E13 无障碍 / E15 离线机房 / E16 性能画像：**NOT VERIFIED** —— 本轮未采集基线，不在报告中假称完成。

### 5. 受保护文件 / 未触碰

`src/agent/**`、`src/agent_ops/**`、`src/science_context/**`、`src/grader/**`、`src/analysis/**`、`src/operators/**`、`src/processing/**`、`src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`resources/translations/**`、`src/main_window*`（#1512 已合并的版本保持原样）。

### 6. 环境说明（可复现性）

本轨在 Windows + MSVC 14.38 + Qt 6.8.0 + 独立 `build-dev` 上构建，**不挂 vcpkg toolchain**（并行轨道持有 `/c/deps/vcpkg/buildtrees/vcpkg-running.lock`，`mv` 报 `Device or resource busy` 证实是 live process），改用 `CMAKE_PREFIX_PATH` + 显式 `INCLUDE`/`LIB`。`vcvars64.bat` 因沙箱拦截 `reg.exe` 而半失效，改用等价的环境脚本。完整踩坑记录见 `.planning/r15-e/BASELINE.md` 第 6 节。
