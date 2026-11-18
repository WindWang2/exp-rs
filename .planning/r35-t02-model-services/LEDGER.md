# LEDGER — R35-T02（goal-loop 持久账本）

格式：第N轮 | 改动 | 验证结果 | 通过/未通过 | 下一步

## 检查点 0（2026-11-19 00:15 前后）

- base=120677e6 (origin/master)，branch=zcode/r35-t02-model-services-20261118T155435Z，worktree 就位。
- 快照核对一致（0 open PR / 29 branches / master SHA）。
- host lock 被 T06 会话 configure 持有（holder pid 9, --wait 600）：第一次排队 --wait 45 退出 75 → 改为 --wait 5400 后台排队（exec_9a189381），未杀任何进程。

## 第 1 轮 | WP01+WP18+WP05 RED 准备 | 待构建验证 | 未通过（等待中）

**改动（全部未提交，源码保持 pristine 供 RED）**：
- 新增 `tests/test_r35_t02_python_worker_deadline.cpp`（4 cases：trickle/silent/segmented/stderr_chatter，1200ms deadline，界 [1000,5000]ms）。
- 新增 fixtures `tests/data/py_worker_{trickle,segmented,silent,stderr_chatter}.py`（stdlib；trickle 有界 8s 防 CTest 挂死）。
- 新增 `tests/test_r35_t02_ensemble_acquire_cancel.cpp`（WP18：b→a→c 成员序、阻塞 factory gate、peer waiter、bounded 5s 退出、c 不启动、retry 不受损）。
- 新增 `tests/test_r35_t02_provider_manifest_typed.cpp`（WP05：catalog typed 矩阵 7+5 token ×2 knob + 历史/null/0/合法往返 + 程序化 ModelInfo 负值拒绝/0=默认 + 已知答案 forward）。
- `tests/CMakeLists.txt`：三处最小追加（test_provider_python 块后×2、test_ensemble_parallel 块后）。
- 修复 patches（git apply --check 全部通过，对 pristine HEAD）：
  - `wp01_wp18_fixes.patch`（readLine QElapsedTimer 绝对 deadline + ensemble cancelPredicate）
  - `wp05_catalog_typed.patch`（provider knobs typed 拒绝，P1-8 deferred 通道）
  - `wp05_provider_domain.patch`（makePythonWorkerRuntime 负值拒绝）

**静态证据（控制流读取）**：
- WP01：`python_worker_provider.cpp:488-506` — 数据到达分支 `continue` 绕过 deadline；仅无数据分支检查；QDateTime 为 wall-clock。
- WP18：`model_ensemble.cpp:418-421` 无 cancelPredicate；对照 `model_execution_service.cpp:247-249` 已传；registry waiter 循环（model_runtime.cpp:1240-1260）已尊重 predicate（100ms cv 粒度），缺口只在调用方。
- WP05：`model_catalog.cpp:496-497` toInt(default) 静默默认（字符串/bool/1.5/-5/1e12 全部变默认）；对照 max_concurrent_members 严格类型先例（995-1012）。

**下一步**：锁释放后 configure+build 5 targets（exec_3f9b5f3a）→ 跑三个新测试取 RED → git apply 三个 patch → 重建 → GREEN → 回归 → R1 review。

## 第 2 轮 | RED 三连实测于 pristine HEAD（120677e6 + 新测试） | 通过（RED 证据成立） | 2026-11-19 ~01:20

构建：`cmake --preset dev-default -DOpenCV_DIR=<共享 opencv5>` 成功（首次裸 preset 找不到 OpenCV，见 DECISIONS#10）；5 targets 100% built（双锁 -j4）。

- **WP01 RED**（`./build-dev/tests/test_r35_t02_python_worker_deadline`，pristine provider）：
  - trickle case：`CHECK(elapsedMs <= 5000)` FAILED，展开 `8084 <= 5000`，message "did not answer within 1200 ms (timed out)" —— deadline 被 8s trickle 完全绕过，真实 QProcess 复现。
  - stderr_chatter case：thrown message 不含 stderr（它在 health().lastError）——测试断言修正为查 lastError（该 case 其余断言 1202ms 有界通过）。
  - segmented/silent 两 case 通过（守卫语义，两版都应通过）。
  - 汇总：4 cases | 2 passed | 2 failed（均为预期 RED/修正点）。
- **WP18 RED**（`test_r35_t02_ensemble_acquire_cancel`，pristine ensemble）：`CHECK(waitUntil(ensembleDone, 5000))` FAILED（false）——父取消后 ensemble 卡在 in-flight waiter 直至 gate 释放；其余 10 断言全过（gate 释放后 late-cancelled、peer/retry 不受损）。1 failed。
- **WP05 RED**（`test_r35_t02_provider_manifest_typed`，pristine catalog/provider）：catalog 矩阵 12 项 refused=false（issues 为空——坏 token 无声通过）；程序化负值 acquire 成功返回 session 且 error 为空（6 项失败）。2 cases | 0 passed | 2 failed（18 failed assertions）。

## 第 3 轮 | 应用修复 → GREEN | 通过

- `git apply` 三 patch 成功；增量重建（T03 曾持锁 exit 75 一次，--wait 7200 重排后完成）。
- `ctest -C Debug -j1`（双锁）：**28/28 passed**——7 个 R35-T02 case 全过（trickle 2.15s，RED 时 8.08s）+ test_provider_python 15 + test_ensemble_parallel 7 全过。

## 第 4 轮 | R1 独立 review（approve，0 P0-P2，6 P3）→ 处置 → 终验 | 通过

- R1：独立 general-purpose 子 agent，读全 diff + 逐项源码核对（详见 REVIEW_R1.md）。verdict **approve**。
- 处置：#2（declared token 诊断）与 #4（docs typed 语义）已修；#1/#3/#5/#6 为有意选择（注释/DECISIONS 记录）。
- 重建 8 targets（含 reviewer 建议的 test_provider_fallback / test_model_manifest7 / test_r33_t02_wire_admission）。
- 终验 ctest：**53/53 passed, 0 failed**（7 新 case 二跑 + 15 provider_python + 7 ensemble_parallel + 3 fallback + manifest7 + 9 r33_t02_wire；FAST 61.32s）。R1 复核建议全部落实。

## 待办

- [ ] R2 复查最终提交 diff
- [ ] 刷新 master/open PR → push → PR（PR_BODY 已备，数字已实测）

## 提交与推送检查点（2026-11-19 ~02:4x）

- 提交：`e631798823` fix(models): bound worker exchanges and type provider knobs (R35-T02)（22 files，+1441/-11；.planning 仅 *.md，patch/.fixed 工作件被白名单排除）。
- 提交前刷新：origin/master 仍 = 120677e6（无新提交）；open PR = 1 → **#2149**（T06 i18n，head dc9dec27a79a）已登记保留区，与本轨零生产/测试重叠（仅 .gitignore 各自追加白名单）。
- 待：R2 复查 `git diff 120677e6..HEAD` → push → gh pr create。
