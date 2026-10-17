## hardening(r15-e): Qt/QGIS 工作台、实验教学、帮助与操作反馈质量

- 分支 `hardening/r15-E-desktop-teaching-help` · BASE_SHA `46791bc65c6f552ebbcf723bb91892736d56f319`（建 worktree 时的 `origin/master`，已含 #1509）
- 主题：**「拒绝/失败已知但不可见」** —— 凡是系统内部已经知道「有意跳过 / 有意失败」的地方，都必须转成用户可见、可复查的信号；禁止裸 `continue`、静默降级和中性图标。

### 0. 先摆事实：任务书快照已过期

任务书写于 #1512 仍 OPEN 时。执行时实测：**#1512 已合并**（`f7693fce1`，2026-10-16T02:35Z），#1505 / #1509 亦已合并，开放 PR 只剩算法/数据/模型/Agent 四个轨道（#1514 / #1513 / #1515 / #1516）。因此「#1512 的 31 个文件只读锁定」不再适用，改为**先读合并 diff、只修合并后仍真实残留的缺陷**；本 PR 未从任何其它分支 cherry-pick。

### 1. 修复索引（4 × P1 + 8 × P2/P3）

| # | WP | 文件 | 缺陷 | 级别 |
|---|---|---|---|---|
| R15E-1 | E05 | `src/app/widgets/rs_result_summary.cpp` | 产物路径未做存在性校验即建成可点击项，双击把不存在的路径送进 `loadRasterLayer` | P2 |
| R15E-2 | E05 | 同上 | 状态行硬编码 `✓ Processing finished`，忽略 result 自带 `status` | P2 |
| R15E-3 | E05 | `src/app/shell/task_panel_host.cpp/.h` | `setFailed()` / `setRunning(true)` 不清上一次的成功结果卡 → 绿色成功与红色失败同屏 | **P1** |
| R15E-4 | E05 | `src/app/shell/workflow_session_controller.cpp` | pipeline 终态从不 `setRunning(false)` → Run 永久停在 "Stop"、表单永久禁用、二次运行被 `m_runInFlight` 拒绝 | **P1** |
| R15E-4b | E05 | 同上 | `cancelActiveRun()` 提前清 id → 步骤永久停在 "running" | P2 |
| R15E-5 | E05 | `src/app/shell/rs_job_panel.cpp` + 新增 `task_progress_text.h` | 失败/取消的任务在列表里仍打印百分比（"Failed \| 50%"）；详情面板则相反，需要保留观测值 | P2 |
| R15E-5b | E05 | `src/app/shell/rs_job_panel.cpp` | 「Clear Finished」只扫顶层行，pipeline 子步骤清不掉 | P2 |
| R15E-5c | E05 | 同上 | Load 按钮只看「有路径」不看「文件存在」 | P3 |
| R15E-6 | E08 | `src/teaching/course_home_view_model.cpp` | `overall_percent` 原样照搬（105% 直接渲染、陈旧 0 与实际完成集矛盾） | P2 |
| R15E-7 | E08 | `src/teaching/lab_feedback_projection.cpp` | evidence 缺布尔 `passed` 被判 `fail`，经 `aggregate()` 把整个实验判为「未通过」 | **P1（误判学生成绩）** |
| R15E-8 | E09 | `src/lab_pack/lab_pack.cpp` | 解析无 `stackLimit`、无字节上限，`Json::Exception` 逃逸出 typed 边界 | P2 |
| R15E-9 | E08/E09 | `src/teaching_admin/batch_assessment.cpp/.h` | 隐藏学生目录 / 隐藏提交文件 / 嵌套目录用裸 `continue` **静默蒸发**（整份作业从 rows/skips/total/checkpoint 同时消失，6 人班报「graded 5 / 5」） | **P1（数据丢失）** |
| R15E-10 | E08 | `src/app/teaching_admin/teaching_admin_dock.cpp` + 新增库函数 `loadBatchReportFromFile()` | 损坏/半写批改报告被静默吞成格式合法的「0 份作业 / 0% 完成」班级汇总 | **P1** |
| R15E-11 | E10 | `data/help/operators/{optical,data_io}.json` + `src/help/operator_help_provider.cpp` | `algorithm.keyParameters` 与算子 schema 漂移 3 处；该字段是唯一未被校验的引用类型 | P2 |
| R15E-12 | E09 | `src/lab_pack/lab_pack.cpp` | Windows 上 `path("/etc/passwd").is_absolute()` 为 false，跨平台 pack 的 POSIX 绝对输入绕过词法包含性检查，被误报成「缺失」而非「越界」 | P2 |

### 2. 测试证据（完整表见 `.planning/r15-e/TEST_RESULTS.md`）

构建：标准 vcpkg 工具链（本轨私有 `VCPKG_INSTALLED_DIR` 副本，含 opencv4 4.12.0），`-j2`，`BUILD_EXIT=0`（`[1958/1958]` + `[892/892]`，0 error）。
运行：`QT_QPA_PLATFORM=offscreen`，两轮逐字一致。

**全绿（9/9）**：`test_r15_E_result_artifact_honesty` 44/4 · `test_r15_E_progress_text` 13/4 · `test_r15_E_teaching_stats_honesty` 33/6 · `test_r15_E_lab_pack_bounds` 8/3 · `test_r15_E_batch_refusal_accounting` 62/9 · `test_r15_E_task_panel_state` 7/2 · `test_rs_result_summary` 29/3 · `test_workflow_session_controller` 35/5 · `test_teaching_lab_cockpit` 185/23。

**RED 实锤**：`test_r15_E_lab_pack_bounds` 修复前 `EXITCODE=42`，输出
`due to unexpected exception with message: Exceeded stackLimit in readValue()` —— 异常确实从 `loadFromBytes()` 逃逸，不是推断。

**既有失败 4 项，全部 A/B 归因为环境既有（非本轨回归）**：

| 目标 | 失败 | A/B 方式 | 结论 |
|---|---|---|---|
| `test_teaching_batch_discovery_r10` | 1（`items.size()==1` 实得 4） | 恢复 master 版 `batch_assessment.*` 重建重跑 | 同样失败 → 与 `QFile::link()` 在本机创建硬链接/快捷方式有关 |
| `test_teaching_admin_core` | 2（`grader_crashed` / `check.ran`） | 同上 | 同样 2 处失败 |
| `test_lab_data_pack` | 1（`REQUIRE(started)`） | 主工作区 2026-10-09 旧 master 二进制 | 同样失败（line 417 那处**已被本轨修好**：11/2 → 12/1） |
| `test_help_coverage` | 21（8 个 `command.rs.*` 命令知识缺失） | 恢复 master 版 `operator_help_provider.cpp` 重建重跑 | 同样 21；本轨新增的 keyParameters 校验在这些运行中**报错 0 条** |

### 3. 去重证据（对 #1513/#1514/#1515/#1516）

- 生产文件**零重叠**：#1513 只动 `src/{cli,data,dataset,experiment,geospatial}`；#1514 只动 `src/analysis|operators|processing`；#1515/#1516 同理（模型运行时 / Agent harness）。本 PR 全部落在 `src/app/**`、`src/help/**`、`src/teaching/**`、`src/teaching_admin/**`、`src/lab_pack/**`、`data/help/**`。
- 共享文件只有两个，均为**纯追加**：
  - `.gitignore`：末尾追加 4 行 r15-E 规则（#1514/#1515/#1516 也各自追加自己的规则 → 预计有 append-only 冲突，按行序解决即可）。
  - `tests/CMakeLists.txt`：追加 6 个新目标块（#1513/#1515 也追加各自块 → 同上）。
- 复核并**证伪**了两条子代理结论：`rs:atmospheric_correction.band` 与 `rs:mosaic.inputs` 经源码比对**并未漂移**（`rs_atmospheric_correction_operator.cpp:40`、`rs_mosaic_operator.cpp:86`），未做修改。

### 4. 仍开放的风险（据实）

1. `WorkflowSessionController::cancelActiveRun()` 仍提前清空 `m_activePipelineId`/`m_pendingTaskId`，因此取消后收不到终态回调、没有「Cancelled」就地反馈。改这一条要推翻 `tests/test_workflow_session_controller.cpp` 里 `CHECK(activePipelineId() == -1)` 的既有断言并重新论证并发语义；本轮只做等价且无风险的步骤状态复位。
2. `src/grader/grader_json.cpp` 的 `parseJsonStrict()` 注释声称会捕获 "Exceeded stackLimit"，但从未设置 `stackLimit`。**所有权不在本轨**（`src/grader/**` 未在允许写入清单内），证据已记录。
3. 「禁用态无原因」在仓库里有 4 处独立实现各自漏一遍（`ribbon_controller::addActionButton` 丢失原因且 `accessibleDescription` 在禁用态被写成通用 tooltip、`qgsgeoref_shell_window` 的 Run 按钮、14 个数字化工具、`rs_job_panel` 右键动作）。仓库已有 3 份正确实现 —— 更合适的形态是收敛成公共件 + 一条源码扫描契约测试，超出本轮预算。
4. `rs_job_panel` 加载产物「部分成功静默」（n>0 但小于请求数时无任何提示）。
5. **NOT VERIFIED**：E11 主题一致性 / E13 无障碍与键盘 / E15 离线机房 / E16 性能画像 —— 未采集基线，`PERFORMANCE.md` **不提供任何数字**。
6. `test_teaching_admin_core` 在本机曾以 Catch2 内部断言（`redirect is already active`）abort（exit 3），最近两轮改为 2 条断言失败；该行为在不含本轨改动的旧 master 二进制上也能复现，属环境/顺序相关的既有问题。

### 5. 受保护文件：未触碰

`src/agent/**`、`src/agent_ops/**`、`src/science_context/**`、`src/grader/**`、`src/analysis/**`、`src/operators/**`、`src/processing/**`、`src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`resources/translations/**`，以及 #1512 已合并的 `src/app/main_window*.cpp`、`qgis_app_facade.*`、`qgsclipboard.*`、`durable_file_write.*`、`cartography/mapspec/layout_tools`。

### 6. 环境说明（可复现性）

Windows + MSVC 14.38 + Qt 6.8.0 + 独立 `build-dev`。踩坑与解法的完整记录在 `.planning/r15-e/BASELINE.md` 第 6 节，摘要：
① 并行轨道的 vcpkg 锁曾阻塞配置（`mv` 报 `Device or resource busy` 证实是 live process）；② 沙箱拦 `reg.exe` 导致 `vcvars64.bat` 半失效，改为显式 `PATH/INCLUDE/LIB`；③ 全局 vcpkg 树早于 manifest baseline（缺 jsoncpp/opencv）→ 复制完整依赖树到本轨并用标准工具链；④ `resources/icons` 是 git 符号链接，`core.symlinks=false` 时被检成普通文件导致 `qrc_icons` 缺文件（用 `ln -s` 还原，`git status` 干净）；⑤ Bash→`cmd //c` 桥接会破坏批处理环境，测试统一走 PowerShell。
⑥ 另发现一个**不在本轨所有权内**的构建配置缺陷：OpenCV 缺失时 `sicnu_processing` 仍无条件编译 `src/processing/algorithms/ir_mad_kernels.cpp`（opencv 的 include/link 却 gate 在 `if(OpenCV_FOUND)`，见 `src/processing/CMakeLists.txt:331`），导致该整库不可构建。
