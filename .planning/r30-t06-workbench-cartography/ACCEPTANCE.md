# R30-T06 · ACCEPTANCE（Oracle F1–F7 对照）

> 最终 HEAD：`30d9afeb7f`；生产 commit `0805c5e72e`（rebase 后；原 `3d6a225d` 的补丁**字节等价**）：
> base 已由 `dcda90bf` 前进到 `606e3c9101`（9 个提交，与本轨 22 个文件零重叠），
> `git diff dcda90bf..3d6a225d` 与 `git diff 606e3c9101..0805c5e72e` 的 sha256 同为 `a611a1d3…`。
> 之后仅追加文档/账本与 `tests/CMakeLists.txt` 的 `TIMEOUT 120` 元数据，生产逻辑无改动。
> 状态口径：`DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `BLOCKED` / `NOT_EXECUTED`。
> **R2 已复核**：未发现 P0/P1 静态阻断项；运行时证据缺失一律如实标注，不冒充通过。

| 编号 | 必须满足的可观察结果 | 状态 | 证据 / 缺口 |
|---|---|---|---|
| F1 | 有效/空/失效组在正确活动 canvas 上缩放或诚实拒绝，无空 canvas 解引用与过期对象操作 | **实现已落地 + 静态证据齐备；运行时 `BLOCKED`** | 修复见 `layer_tree_menu.cpp/.h`、`active_view_host.h`；R1 P1-2/P1-3 已修、R2 静态复核通过；5 个新 case（含触发后 extent 数值、活动视图切换、纯投影拒绝）。**`cmake --build` / `ctest` 未实跑**（见下） |
| F2 | 历史所有入口使用同一资格和身份，具体双方产物可比较；失败提交不显示 queued | **实现已落地 + 静态证据齐备；运行时 `BLOCKED`** | 修复见 `processing_history_panel.cpp/.h`；R1 P1-1/P2-2、R2 P2-2 口径复核通过；8 个新 case（菜单/按钮/`ActionPlan` 一致、`QSignalSpy` 两侧非首产物、缺失产物不配对）。**旧回归 `test_r29_t04_history_actions`（P1-4）未实跑确认** |
| F3 | 大 collection 所有记录可有界到达，选择/过滤/刷新/项目切换不串 asset | **NOT_EXECUTED** | C5 已确认（`data_manager_panel.cpp:893-937` vs `:1130-1158`）；本轮不实施，见 `HANDOFF.md H05` |
| F4 | 视图、图层、样式与制图输入关联明确，同源不同显示实例不被错误合并 | **NEEDS_REPRO / NOT_EXECUTED** | C4：静态去重字符串不等于证明错误，本轮不凭字符串断言 |
| F5 | 草稿/编译/Designer/检查/修复/导出指向同一明确修订，失败和旧布局状态准确 | **NOT_EXECUTED** | C3 已确认（`cartography_dock.cpp:566,496,585/613/687`）；本轮不实施 |
| F6 | 多页预览、质量问题与交付文件/manifest 可检查，内容验证不只断言文件存在 | **NOT_EXECUTED** | C6 已确认（`:531-535,723-728,766-785`）；本轮不实施 |
| F7 | 三条完整链、必要 Qt/生命周期回归和实测体验证据绑定最终 HEAD，独立 review 完成并提交 PR | **部分 / NOT_EXECUTED** | R1→fix→R2 **已完成**（`REVIEW_R1.md` / `REVIEW_R2.md`）；三条完整链、Qt 生命周期回归、实测体验**未执行**；PR 以 Draft 提交并明确限制 |

## 运行时验证状态（诚实记录）

| 项 | 状态 | 真实证据 |
|---|---|---|
| 4 个 TU `-fsyntax-only` | ✅ DONE | `TOTAL_FAILURES=0`（2 个生产 + 2 个新测试各 `exit=0`）；期间抓到并修复 `addFeatures` 非常量左值引用的真实编译错误 |
| `git diff --cached --check` / `git diff --check dcda90bf..3d6a225d` | ✅ DONE | 均 `EXIT=0` |
| 独立 R1 → fix → 独立 R2 | ✅ DONE | `REVIEW_R1.md`（3 P1 + 1 待实跑 P1 + 4 P2 + 4 建议，逐条处置）、`REVIEW_R2.md`（无 P0/P1 静态阻断） |
| `cmake --preset dev-default -DOpenCV_DIR=…`（双锁 + `-j4`） | ⚠️ **BLOCKED** | 生成阶段反复被宿主中断：`build-dev/tests/CTestTestfile.cmake` 始终未生成，`build.make` 最多 2238/4133；已尝试双锁 `--wait 45/30/20`、零 wait 单次 guard、前台 600 s、后台 `--wait 5400`、沙箱豁免（详见 `DECISIONS.md D20`） |
| `cmake --build --parallel 4 --target <5 targets>` | **BLOCKED** | 依赖 configure 完成 |
| `ctest -N` / `--show-only=json-v1` 核对真实 case 数与 prefix | **NOT_EXECUTED** | 依赖 build；**未核对，不能声称 0 tests 之外的情况** |
| 新增回归 + 直接受影响既有测试（`test_r29_t04_history_actions`、`test_processing_history_model`、`test_r22_workbench_view_tree`）实际运行 | **NOT_EXECUTED** | 依赖 build；P1-4 旧文案兼容性未实跑 |
| RED（base 上稳定失败） | **NOT_EXECUTED，已给边界证明** | base 触发组缩放会 SIGSEGV，Catch2 无法进程内捕获；最终测试含修复后新增 API 的行为断言，无法在 base 上编译。替代证据：`ISSUE_TRIAGE.md` 逐行调用链（`:95` → `qgslayertreeviewdefaultactions.cpp:129-134,397-402,435/445,456`）+ `REVIEW_R1.md` 静态核对 |
| 高 DPI / 焦点 / 明暗主题 / 屏幕阅读名称真实桌面证据（WP11） | **NOT_EXECUTED** | Qt offscreen 不足以证明 |

## 本轮「完成」的严格口径

本轮 PR **只能声称 F1 + F2 的实现与静态验证完成**，不能声称 F1/F2 已运行时验收通过，更不能声称 F3–F7 完成。
按任务「必要环境…缺失时先提交可审查的 Draft PR 并明确 BLOCKED/NOT_EXECUTED；这不满足『全部开发完成』」执行。
恢复入口见 `RESUME.md`：环境具备后**第一条动作**是补齐 configure → `ctest -N` 核对真实 case 数 → 跑新增回归与 3 个既有回归 → 复核 P1-4 → 再请 reviewer 做最终差异复核。

## 新测试用例数（R2-2 修正）

- `tests/test_r30_t06_group_zoom.cpp`：**5** 个 `TEST_CASE`（`TEST_PREFIX "test_r30_t06_group::"`，`TIMEOUT 120`）
- `tests/test_r30_t06_history_action_parity.cpp`：**8** 个 `TEST_CASE`（`TEST_PREFIX "test_r30_t06_history::"`，`TIMEOUT 120`）
- 合计 **13** 个；均为新增，未改动 `test_r29_t04_history_actions.cpp`。
