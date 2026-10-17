# R15-E REVIEW ROUND 1 — 独立审查

## 审查者独立性声明（如实）

- 审查由**另一个未参与编写的 agent 会话**执行（独立上下文，读 `git diff HEAD` 全文逐文件审查）。
- **同一模型**，不是人类 reviewer，也不是仓库里其它轨道的 agent。
- 任务书要求的 `/teamwork-preview` 团队协作入口在本宿主不存在；本轨按任务书
  「若发现未安装，记录未安装/等价执行事实，禁止声称成功调用不存在的命令」处理：
  **未调用** `/teamwork-preview`，改用「独立会话 + 只读 diff 审查 + 两轮」的等价流程，并在此显式声明。
- 审查者的自述限制：无法做运行时验证、无法在其它 OS/真机验证、未读被改文件的完整历史。

## 审查者结论

**存在必须修的 P1（1 条）**，其余为 P2/P3。

## 发现清单与处置

| # | 级别 | 位置 | 问题 | 处置 |
|---|---|---|---|---|
| R1-1 | **P1** | `src/teaching_admin/batch_assessment.cpp` 原 387/461 行 | **双重记账**：根级隐藏条目被「学生目录 pass」（`studentId` 错填成该条目自身文件名）与「根级扁平 pass」（`studentId` 为空）各 `recordSkip` 一次 → 一个文件两条 skip、计数虚高、控制台显示「N files skipped」翻倍 | **已修** —— ① 目录 pass 只在 `subInfo.isHidden() && subInfo.isDir()` 时记账；② 扁平 pass 把隐藏判定**移到** `isSymLink()/!isFile()` 之后，只处理隐藏文件。新增断言 `skipCount(path) == 1` 与「根级隐藏文件 `studentId` 必须为空」 |
| R1-2 | P2 | `src/app/widgets/rs_result_summary.cpp` `isFailureStatus` | 漏了 `"fail"`（grader/projection 语汇里就是 `fail`），`status:"fail"` 的结果仍显示 `✓` | **已修** —— 加入 `"fail"`；测试改为遍历 7 种拼写（含大小写/空白） |
| R1-3 | P2 | `src/teaching/course_home_view_model.cpp` | 新分母按「每个模块的 lab」累加，而上游 `curriculum_progress.cpp` 按 lab id 去重；同一 lab 被两个模块列出时分母偏大 | **已修** —— 改为按 `labId` 去重（`std::map<std::string,bool>`），并新增「同一 lab 出现在两个模块 → 分母仍为 1」的用例 |
| R1-4 | P2 | `tests/test_r15_E_teaching_stats_honesty.cpp` 首个用例 | 该用例**不可证伪**：`labsTotal > 0` 时派生分支必然覆盖上游值，`105` 与 `100` 都是 0，断言恒真 | **已修** —— 新增零 lab 课程清单用例，使夹取路径**可达**并断言 `105 → 100`、`-7 → 0`；原用例保留为回归（并改注释说明它验证的是派生优先） |
| R1-5 | P2 | `src/teaching_admin/batch_assessment.cpp` `loadBatchReportFromFile` | `rows` 元素为非对象时被 `toObject()` 静默转成空对象 → 又变回「静默少报」 | **已修** —— 非对象行 → `error` + `false`；新增用例 |
| R1-6 | P2 | 本轮 `TEST_RESULTS.md` 的措辞 | 声称「105 → 夹取到 100 被断言」在当时的测试里并不成立 | **已修** —— 该断言现在真实存在（R1-4）；文档在定稿时同步更正 |
| R1-7 | P3 | `src/app/widgets/rs_result_summary.cpp` | warnings 隐藏时不清文本，下一份文档的提示会与上一份拼接 | **已修** —— 两个隐藏分支都 `clear()` |
| R1-8 | P3 | `src/app/shell/rs_job_panel.cpp` `fillDetailsForTask` | 详情面板也显示 `Progress: —`，丢失「跑到 50% 才失败」这一有用信息 | **已修** —— 新增 `formatProgressObserved()`，详情面板用观测值，列表/列仍用 `—`；规则抽到 `src/app/shell/task_progress_text.h` 纯函数，另有轻量测试 |
| R1-9 | P3 | `src/app/shell/task_panel_host.cpp` `setFailed` | 无条件清结果卡 → 提交被拒绝等「并未真正运行」的失败也会抹掉上一份仍有效的结果 | **接受但保留**（判定依据反向）：E05 的 oracle 明确是「失败/取消不显示绿色完成」，而「红色错误 + 绿色成功卡同屏」正是被举报的缺陷。`setRunning(true)` 已覆盖真实运行失败路径，`setFailed` 再清一次使组件不依赖调用方顺序。代价仅限「未运行的操作性拒绝会清掉仍有效的结果卡」，记录为已知取舍 |
| R1-10 | P3 | `src/app/shell/rs_job_panel.cpp` 清除完成行 | 子行的 `m_loadToMain` 偏好未随父行删除而清理 | **已修** —— 删除父行前先移除其子行 id 的偏好 |
| R1-11 | P3 | `src/app/teaching_admin/teaching_admin_dock.cpp` 600-604 | 续行缩进与文件风格不一致 | **已修** —— 抽出 `reportPath` 局部量 |

## 审查者确认无问题的事项

- `collectTerminalRows()` 的「子行全终态时只删父行、否则保留父行」不会产生悬垂/重复删除，也与「父任务已取消但仍有子步在跑」的真实状态吻合。
- `lab_pack` 的 8 MiB 上限不会误杀合法 manifest：repo 内 `data/labs/packs/` 的实际 pack 只有脚本会写 ≤64 MiB 的**批改检查点**（那是另一个加载器），manifest 是文件索引，KB~低 MB 级。
- `formatProgress` 改为按状态输出 `—` 未改变任何科学产物。
- 未发现与 #1501/#1504/#1506–#1512 重复的修复。

## 审查者指出但**未在本轮修**的项

| 项 | 理由 |
|---|---|
| `cancelActiveRun()` 的 id 提前清空 → 取消后无「Cancelled」终态反馈 | 会被 `tests/test_workflow_session_controller.cpp` 的 `CHECK(activePipelineId() == -1)` 推翻，属行为契约变更；本轮只做步骤状态复位 |
| 各轨 `tests/CMakeLists.txt` 并发编辑冲突风险 | 本 PR 只追加自己的块，未改他行；已记录 |
