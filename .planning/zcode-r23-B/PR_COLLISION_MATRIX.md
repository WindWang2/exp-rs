# R23-B PR_COLLISION_MATRIX

## 起始时点
Open PR = 0；master = ad98a7a704。无历史冲突。

## 并行轨写集（2026-10-27 01:2x 现场核查）
| 轨 | worktree | 已改文件 | 与本轨交集 |
|---|---|---|---|
| r23-A | exp-rs-r23-A-20261026T164112Z | tests/test_math_utils.cpp | 无 |
| r23-C | exp-rs-r23-C-20261026T164126Z | src/processing/framework/task_center.{cpp,h}、tests/CMakeLists.txt（append）、tests/test_r23_c_task_center_log_spaces.cpp（新） | **tests/CMakeLists.txt 共享尾部**（双 append，合并时 union，见下） |

## tests/CMakeLists.txt 协议
- 本轨仅追加两个自包含注册块，均带 `# --- end R23-B ... registration ---` 终界：provider stub-compile gate、derived-output-roles、run-metric-transaction（第三个块）。
- push 前 `git fetch origin --prune` 后 `git show origin/master:tests/CMakeLists.txt` 与工作树逐块比对：所有兄弟注册块（R20/R21/R22/r23-C 等）完整在场；若 master 已前移含他轨新块，merge 后保留双方（union），cmake 重配置 + 定向实跑后再 push。
- 禁止 ours/theirs 整文件覆盖（#1714/#1715 教训：冲突标记事故）。

## 生产文件
本轨触碰的生产文件（openvino_provider.cpp、tensorrt_provider.cpp、tile_inference_engine.cpp、experiment_store.{h,cpp}、reproduction_bundle_import.cpp、probe_remote.cpp、process_network_policy.h 注释）与 r23-A/C 当前写集零交集；与已合并 r21-B/r22 的触碰均为先后关系（无并发）。

## push 前复核清单
1. git fetch origin --prune；git diff --name-only origin/master...HEAD 对照上表。
2. tests/CMakeLists.txt 注册块完整性（本轨 3 块 + 全部兄弟块）。
3. git diff --check；无冲突标记。
