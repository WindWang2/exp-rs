# R15-E FINAL REPORT

## 1. 身份与坐标

| 项 | 值 |
|---|---|
| 轨道 | R15-E · Qt/QGIS 工作台、实验教学、帮助与操作反馈质量 |
| 分支 | `hardening/r15-E-desktop-teaching-help` |
| worktree | `exp-rs-r15-e-20261017`（`C:/Users/wangj.KEVIN/projects/` 下） |
| BASE_SHA | `46791bc65c6f552ebbcf723bb91892736d56f319`（= 建 worktree 时的 `origin/master`；收尾时 `git fetch` 复核**未前进**，`0 behind / 5 ahead`，无需 rebase） |
| HEAD_SHA | `00702d97bb2d1dcf3e020ad5a510d0fe2cf5e18d` |
| PR | https://github.com/WindWang2/exp-rs/pull/1517 |
| 构建树 | `build-dev/`（本轨独立，标准 vcpkg 工具链 + 私有 `vcpkg_installed` 副本） |

## 2. 改动规模

`git diff --stat 46791bc6..HEAD -- src tests data .gitignore` → **25 files changed, 1605 insertions(+), 109 deletions(-)**，另有 `.planning/r15-e/` 下 10 份审计/证据文档。

- 生产文件 15 个：`src/app/shell/{rs_job_panel,task_panel_host,workflow_session_controller}`、`src/app/shell/task_progress_text.h`（新增）、`src/app/widgets/rs_result_summary.cpp`、`src/app/teaching_admin/teaching_admin_dock.cpp`、`src/teaching/{course_home_view_model,lab_feedback_projection}.cpp`、`src/teaching_admin/batch_assessment{.cpp,.h}`、`src/lab_pack/lab_pack.cpp`、`src/help/operator_help_provider.cpp`、`data/help/operators/{optical,data_io}.json`
- 测试 7 个：6 个新增（`test_r15_E_*`）+ 1 个既有 fixture 修正（`test_rs_result_summary.cpp`）；`tests/CMakeLists.txt` 纯追加

## 3. 问题归类

| 归类 | 内容 |
|---|---|
| `FIXED_ON_MASTER`（只做回归） | #1488/#1486/#1485（#1509/#1505）、矩阵表 bounded 与 dock 线程（#1510）、教学证据（#1511）、矢量编辑/MapSpec/原子写（#1512） |
| `REPRODUCED_OPEN`（本轨复现并修复） | R15E-1 … R15E-12，共 12 项（4×P1 + 8×P2/P3），见 PR_BODY 第 1 节 |
| `NOT_REPRODUCIBLE`（复核证据） | 子代理报告的 `rs:atmospheric_correction.band`、`rs:mosaic.inputs` 两处「帮助参数漂移」经源码比对**证伪**（`rs_atmospheric_correction_operator.cpp:40`、`rs_mosaic_operator.cpp:86`），未改 |
| `OUT_OF_SCOPE`（所有权越界） | `src/grader/grader_json.cpp` 缺 `stackLimit`（证据已记录，建议交对应轨道）；`src/processing/algorithms/ir_mad_kernels.cpp` 在 OpenCV 缺失时不可编译 |
| `DEFERRED_WITH_EVIDENCE` | `cancelActiveRun()` 的终态反馈（需推翻既有断言）、禁用态原因的 4 处重复实现、`rs_job_panel` 部分加载静默、帮助 guidance 死代码/裸 id 渲染 |

## 4. RED → GREEN 证据

见 `TEST_RESULTS.md`。要点：

- `test_r15_E_lab_pack_bounds` 修复前 `EXITCODE=42`，`due to unexpected exception: Exceeded stackLimit in readValue()` —— **异常逃逸实锤**（不是推断）。
- 两轮逐字一致，**9/9 目标全绿**：44/4、13/4、33/6、8/3、62/9、7/2、29/3、35/5、185/23 断言/用例。
- 4 个既有失败全部用 A/B 归因为环境既有：恢复 master 版代码重建重跑（2 个）、旧 master 二进制（1 个）、恢复 master 版 `operator_help_provider.cpp`（1 个）。
- 附带修复了 `test_lab_data_pack` 的一处真实越界判定（11/2 → 12/1）。

## 5. 失败 / 限制

见 `PR_BODY.md` 第 4 节。其中 E11 / E13 / E15 / E16 明确标注 **NOT VERIFIED**；`PERFORMANCE.md` 不含任何性能数字（本轮未采集基线，不做无据优化）。

## 6. 独立 reviewer

| 轮次 | 结论 | 处置 |
|---|---|---|
| Round 1（独立会话，只读全文 diff） | 1×P1 + 5×P2 + 5×P3；**无 P0** | 全部 P1/P2 已修；P3 中 4 条已修、1 条显式接受并记录取舍（`setFailed` 清结果卡）；详见 `REVIEW_ROUND1.md` |
| Round 2（独立会话，复核第一轮修法 + 新增 `lab_pack` 修复） | 见 `REVIEW_ROUND2.md` | — |

**独立性声明**：两轮均由**未参与编写的独立 agent 会话**执行，但**同一模型**、非人类 reviewer；
任务书要求的 `/teamwork-preview` 团队入口在本宿主不存在，按「禁止声称成功调用不存在的命令」处理 —— **未调用**，
改用「独立会话 + 只读 diff + 两轮」的等价流程并在此显式声明。

## 7. 提交

```
00702d97b docs(r15-e): final PR body with dedup and A/B evidence
c2883be2f docs(r15-e): final two-round test evidence and A/B triage of pre-existing failures
5bc7b89ee fix(lab_pack): reject a leading separator lexically in pack containment
440405b3b docs(r15-e): baseline, ownership, oracles, ledger and review-round-1 records
e3887757e fix(ui,teaching,help): make deliberate refusals and read failures visible (r15-E)
```

## 8. 未做的事（明确）

- **未合并** PR（任务书禁止自动合并，需用户显式确认）。
- **未等待也不使用远端 CI** 作为通过口径。
- **未改** `master`、未改其它 worktree、未 `reset --hard` / `clean -fd`。
- **未提交** 任何构建日志、绝对路径清单之外的机器信息或大数据集；`.planning/r15-e/` 只含 markdown。
