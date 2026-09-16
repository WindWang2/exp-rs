# PARALLEL_OWNERSHIP — F20 context-help-diagnostics-11

启动时（2026-09-16，master=a5b11b7f10）open PR / remote branch 与本 track 的文件级交集与策略。

## PR #1009 `zcode/execution-runtime-convergence-11`（execution-11，MERGEABLE）

changed files 中与本 track 相关的交集：

| 文件 | #1009 的改动 | 本 track 策略 |
|---|---|---|
| `data/help/diagnostics.json` | **文件尾 append** 2 个 operator 页（CorruptArtifactData / ResourceBudgetExceeded） | 本 track 对该文件的新增**只插入中段 harness 区之后**（diagnostic.harness.* 相邻区域），不在文件尾 append，避免文本冲突；不重做/不复制其 2 页；不删除其条目 |
| `src/operators/framework/rs_operator_error.{h,cpp}` | append 2 个错误码 | **read-only**：只消费既有 ErrorCode 枚举/映射做 D 包（错误码→诊断页），不修改该文件 |
| `CHANGELOG.md` | append 条目 | 本 track 的 CHANGELOG 增量放到独立 integration commit，append 于文件头部现有格式区 |
| `.gitignore` | 未知改动 | 本 track 只 append 一行 `!.planning/context-help-diagnostics-11/`（独立 integration commit） |
| `tests/CMakeLists.txt`、`src/operators/CMakeLists.txt` | append 新测试 target/源文件 | 本 track append 自己的 target 行于帮助测试聚集区，最小 diff |

依赖判断：本 track **不依赖** #1009 的新 API（其 2 个新错误码属于其执行底座；本 track 的 D 包针对 master 上已存在的码系）。若 rebase 时 #1009 已合入，则其 diagnostics.json 追加与 rs_operator_error 新码成为 master 事实，届时需为这 2 个新码确认 curated 页存在（其 PR 已自带），并重跑 census gate。

## PR #1008 `zcode/radiometric-spectral-workbench`（spectral，CONFLICTING）

与本 track primary scope **零交集**（widgets/spectral_*、analysis/atmospheric、core/radiometric_state、agent/spatial_tools）。共享文件仅 .gitignore / tests CMakeLists / 各 CMakeLists——同样走最小 append。其新增的 agent spatial tools 若日后需要帮助条目，属 follow-up。

## Open Issues #1001–#1007

全部为 dataset/workflow/io/georef 域 fail-open/CRS bug。与本 track 无所有权关系；不认领、不修复，写 EVIDENCE `OUT_OF_SCOPE` 记录。

## 本 track primary write scope（收窄后）

- `src/help/**`（resolver、availability facts、i18n 骨架、markdown writer、census 工具）
- `data/help/**`（diagnostics.json harness 区、commands.json、concepts.json、operators/*.json、help_content.qrc）
- `src/app/help/**`、`src/app/dialogs/dialog_help_catalog.*`、错误呈现接线点（最小接触：`src/app` 中任务/消息错误路径的 1–2 个文件）
- `tests/test_help_core.cpp`、`tests/test_help_coverage.cpp`、`tests/test_i18n.cpp`、`tests/test_diagnostics_contract_9.cpp`、`tests/test_help_system.cpp`、`tests/test_global_help_tips.cpp` 及本 track 新增测试
- `docs/help/**`、`docs/generated/help/**`
- `.planning/context-help-diagnostics-11/**`

## 共享 integration 文件（最小 append-only，独立 commit）

`.gitignore`、`CHANGELOG.md`、`tests/CMakeLists.txt`、（如需）`src/help/CMakeLists.txt`、`src/app/CMakeLists.txt`。
