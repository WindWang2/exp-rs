# BASELINE — Track 12: Teaching & Lab Pack R4 (`hardening/r4-teaching-lab`)

实测时间：2026-09-27。所有数字为本机实测，非沿用提示词。

## 1. 仓库基线

| 项 | 实测值 |
|---|---|
| `origin/master` | `15e5c66b543ef3874cb929f17529ef456bd6c059`（= PR #1333 合并点，fetch 后无前进） |
| 本地 master 分支 | 落后 origin 159 commits（并行轨道都在 worktree 工作，master 只读） |
| 本轨道 worktree | `/home/kevin/projects/rs-studio/exp-rs-teaching-lab-r4`，分支 `hardening/r4-teaching-lab` |
| 开放 issue | 0（`gh issue list --state open` 为空，实测） |
| 工具链 | GCC 16.2.1 / CMake 4.4.3 / Ninja 1.13.2 / 62G RAM |
| 构建目录 | `build-dev`（dev-default preset：Debug + ENABLE_TESTS=ON，全新目录） |

## 2. 在途 PR 盘点（2026-09-27 实测，gh pr list）

| PR | 分支 | 文件数 | 与本轨道白名单交集 |
|---|---|---|---|
| #1334 | fix/review-p1-security | 30 | 无（agent/security 侧） |
| #1335 | fix/review-p0-build-restore | 19 | **`src/agent/harness/lab_copilot.cpp`、`tests/test_lab_data_pack.cpp`、`tests/test_lab_grading.cpp`、`tests/test_teacher_credential.cpp`、`tests/CMakeLists.txt`** |
| #1336 | hardening/closure-ui-runtime-r4 | 13 | **`src/lab_pack/lab_pack.cpp`、`scripts/gen_lab_packs.py`**（提示词声称还改 `tests/test_lab_data_pack.cpp`，实测**不改**——写作信息漂移） |
| #1337 | hardening/closure-workflow-contracts-r4 | 25 | 无 |
| #1338 | hardening/closure-io-processing-r4 | 29 | 无 |
| #1339 | hardening/r4-i18n-help | 38 | `src/app/teaching/**`（GUI 层，不在白名单 `src/teaching/` 内，无冲突） |
| #1340 | hardening/r4-operator-oracles | 24 | 无 |

### 重叠处置（时序决定）

- **#1336 未合并 → `src/lab_pack/lab_pack.cpp`、`scripts/gen_lab_packs.py` 冻结**。先做 teaching/teaching_admin 侧（WP-B/C/D/E/F），WP-A 的矩阵先落在白名单 admin 层（`data_pack_manager.cpp` + `json_util.h` + `feedback_pack.*`），#1336 合并后 rebase 再把矩阵延伸进 lab_pack 权威链。
- **#1335 未合并 → `lab_copilot.cpp:432` 在 master 上仍是 `return diff;`（反转缺陷，本机实读确认）**。WP-D 自行携带该一行修复（白名单扩展点允许，逐处记账），与 #1335 的同名修复 rebase 时按"同内容一行"天然收敛。#1335 已覆盖的 `test_teacher_credential.cpp` 单路径用例**不重复**；WP-D 做三路径（lab_copilot / autonomy gate / grading）一致性。
- **#1335 也负责接线孤儿测试 `test_harness_lab_evals`、`test_lab_grading`、`test_teacher_credential`** —— 本轨道不接这 3 个。

## 3. #1336 描述提取（对本轨道的输入）

- 已修（勿回退）：`lab_pack.cpp` `fileSha256()` 现哈希"git 会存的字节"——前 8000 字节含 NUL 判二进制（二进制原样），否则 CRLF→LF 规范化、孤立 `\r` 保留，64KiB 分块流读。Linux 摘要逐位不变。
- **未解决项 #3 直接移交本轨道**：`src/teaching_admin/data_pack_manager.cpp:258-259` 仍用 `fi.size()` 比对 declared bytes（同类缺陷）。实测补充：**同文件 226 行 `sha256OfFile(abs)`（json_util.h，Qt 层）也按磁盘原始字节哈希**，与已修权威不同源——两处一并收进 WP-A。
- 其余未解决项（簇 A/B/E 生命周期、C++ 测试未在 #1336 分支实跑、帮助目录其余缺口）不属本轨道。

## 4. #1335 描述提取（对本轨道的输入）

- P0-1：`return diff;` → `return diff == 0;`（c2b0b938/#1202 回归）。配 `test_teacher_credential.cpp` 5 用例（同长翻转逐字节拒绝/长度不等/空串/非串/缺失/env 未设空值 fail-closed）。本轨道 WP-D 不重复这些断言，做**跨路径一致性**。
- **142 个既有失败测试分类**与本轨道 ctest 过滤面（lab|teaching|pack|copilot|autonomy）的交集：
  - `lab_batch 退出码`（第 5 组，科学/逻辑断言类）——基线实测对账。
  - `launcher_parity_conformance / gen_lab_packs.py`（第 8 组）——根因是 `sicnu_configure_test_harness_env` 把 PYTHONHOME 钉到 3.12 而 PATH python3 是 3.13，环境错配非产品缺陷；`test_lab_data_pack` 会写版本控制的 `lab_pack_gen_check_stdout.txt`（P3 卫生，勿提交该文件改动）。
  - i18n 漂移组（data_manager_panel 等 14 个）由 #1336 处理，与本过滤面无关。

## 5. 锚定表复核（实测）

| 锚点 | 提示词写作值 | 本机实测 | 结论 |
|---|---|---|---|
| teaching_admin 头文件 | 15 | **15**（admin_types/admin_errors/student_projection/script_adapters/rubric_builder/release_preflight/operator_catalog/labspec_authoring/grader_cli_adapter/feedback_pack/data_pack_manager/curriculum_editor/class_summary/batch_assessment/json_util） | 一致 |
| teaching 头文件 | 7 | **7**（lab_step_timeline/lab_status/lab_session_state/lab_readiness/lab_feedback_projection/course_home_view_model/autonomy_effective_display） | 一致 |
| 22 头清点单位 | 22 | 22 | 一致 |
| lab/teach/pack/copilot 测试文件 | 33 | **33** | 一致 |
| #1336 重叠文件 | 3 | **2**（test_lab_data_pack.cpp 实测不在其变更列表） | 漂移已记录 |
| `lab_copilot.cpp:432` | `return diff;`（未修） | **`return diff;` 确认在基** | WP-D 携带修复 |

## 6. 既有测试接线状态（实测核对，两次勘误后定稿）

第一次用裸 `add_executable` 提取得出"16 孤儿"是**错误的**——多数测试经 `sicnu_add_test()` 宏（tests/CMakeLists.txt 中 186 处）注册。用 `ninja -t targets all`（4052 目标）逐一核对后定稿：

**33 个测试文件全部已接线**（`helper_teaching_fake_grader_cli.cpp` 为共享 helper 编译单元，非独立目标）。无孤儿需复活。本轨道新增测试目标（矩阵/三路径/parity 等）注册进 `tests/CMakeLists.txt` 即可，不与 #1335 的接线 hunk 冲突（其新增 `test_teacher_credential` 目标同理，rebase 时按追加处理）。

## 7. 基线红绿分布

构建 15 个已接线目标后跑：
`ctest -R "lab|teaching|pack|copilot|autonomy" -j1`（结果追加于此）

（占位——build 完成后回填）

## 8. 本轨道边界声明

白名单：`src/lab_pack/`、`src/teaching/`、`src/teaching_admin/`、`src/lab/`、`src/agent/harness/lab_copilot.cpp`（唯一扩展点，逐处记账）、`scripts/gen_lab_packs.py`（#1336 合并后）、`tests/test_lab*`、`tests/test_teaching*`、`tests/test_*pack*`、`tests/test_*copilot*`、`tests/CMakeLists.txt`、`.planning/teaching-lab-r4/`。

明令禁止：白名单外目录；新增功能方向/工作台/算子/实验；等待线上 CI；动共享快照文件；提交 `lab_pack_gen_check_stdout.txt` 的改动。
