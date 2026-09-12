# EVIDENCE — LabSpec Data-Driven Labs

Local-only evidence (no CI). Append per phase; every claim carries the command that produced it.

## Environment

- Worktree `/home/kevin/projects/rs-studio/exp-rs-lab-spec-data-driven`, branch `zcode/lab-spec-data-driven`
  cut from `origin/master` @ `27b9aa0a63` (goal-cited `efc5c52f` is an ancestor; see DECISIONS #1).
- Build: `build/` Ninja, Debug, `ENABLE_TESTS=ON`, `ENABLE_SANITIZERS=OFF`,
  `CMAKE_BUILD_PARALLEL_LEVEL=2`; ccache warm (211k hits, 78% hit rate pre-run).
- Host: 16 cores / 62 GB; caps `-j2` build, `CTEST_PARALLEL_LEVEL=1`; resource log below.

## Phase 0 — baseline audit (static evidence)

- 10 hardcoded factories: `src/app/widgets/guided_workflow_widget.cpp:100-109` +
  `createXxxWorkflow()` definitions (lines 245-810). Header lists all ten (guided_workflow_widget.h:72-81).
- `WorkflowStep::actionId` free-form string: `guided_workflow_widget.h:24`; invoked via
  `QMetaObject::invokeMethod(m_mainWindow, ...)` (guided_workflow_widget.cpp:200).
- 7 documented labs: `docs/labs/README.md:19-59`, files `lab1_image_enhancement.md` … `lab7_image_fusion.md`.
- Stale menus in docs: `README.md:23` (`Raster > Enhancement`) etc.
- Widget live in shell: `main_window_docks.cpp:292`, `main_window_menus.cpp:644`,
  `main_window_misc.cpp:229 showGuidedWorkflows()`.
- Existing test = struct-field round-trips only: `tests/test_guided_workflow_widget.cpp:6-116`.
- Operator registry: `sicnu::processing::AtomicAlgorithmRegistry` (atomic_algorithm_registry.h),
  populated from `RSOperatorRegistry`; 110 `rs:*` ids enumerated from
  `src/operators/rs/rs_operators_init.cpp` (list in CAPABILITY_MATRIX.md §B).
- Shared param validation: `validateParameters()` — `src/processing/framework/schema_validator.h:58`.
- Dialog execution seam: `runOperatorTask` → `JobRequest{algorithmId, params}` → `GuiJobHandle::submitJob`
  (`raster_processing_dialog_base.cpp:486-530`, `src/app/shell/gui_job_adapter.h:30-36`).
- Data path resolution: `resolveRuntimeDataPath("data/<dir>")` (`runtime_paths.h`, header-only;
  `SICNU_DATA_DIR` env override).
- Tests infra: `sicnu_add_test()` (tests/CMakeLists.txt:48) links processing stack;
  `test_guided_workflow_widget` bare Qt target (tests/CMakeLists.txt:5258-5269).
- `.gitignore:35-54`: `data/*` with per-dir negations — pattern to extend for `data/labs/`.
- Configure log: `.planning/lab-spec-data-driven/configure.log`.

## Resource log (60 s samples during builds)

(appended per phase)

## Phase 1+ evidence

- P1 commit `8051d7a162` — schema + gitignore; probe evidence:
  `git add data/labs/probe.lab.json` ⇒ status `A`（trackable）；`git check-ignore -v
  data/samples/x.tif` ⇒ `.gitignore:35:data/*`（样本仍忽略）。
- P3 commit `7fc57ce967` — 11 labs，Python jsonschema 预检 0 errors（命令与输出见
  PROGRESS 会话 1）。
- P4 commit `3980de6cf6` — `python3 scripts/gen_lab_docs.py --check` ⇒ `ok: 12 generated
  doc(s) in sync`，exit 0。
- P5 commit `bc4fecf145` — LABSPEC.md + ADR 0146；`--check` 仍 0（白名单 LABSPEC.md）。
- P2 commit `9b7fcb45e2` — loader/widget/tests/CMake。
- P6 reviewers（两个只读 subagent）：
  - 静态核验 G1–G10 全 PASS @ 9b7fcb45e2（labs tracked 11 文件；jsonschema 0 errors；
    id↔文件名一致；三清单 11/11/11；`--check` exit 0 且 `git status docs/` 干净；
    diff 45 路径全部在允许集；0 个 createXxxWorkflow 残留；tr() 合规；规划文件齐；
    5 个提交、master 未动）。
  - 对抗审查：1×P0 + 3×P1 + 8×P2（详情与处置见 REVIEW_LOG.md）。
- P7 commit `1f63ee3386` — 12 项发现全部处置（P0 stem 检查、lab11 training 绑定、
  dup-id 死码删除 + 测试重写、运行按钮状态机、8×P2 加固）；文档同 commit 重生成。
- 构建（全量新 worktree，无 ccache 可用——仓库未配 COMPILER_LAUNCHER 且启用 PCH）：
  configure 成功（FETCHCONTENT_SOURCE_DIR 指向本地副本）；ninja 起始 -j2，00:48 负载
  24.45 > 1.5×16，按硬约束降为 -j1 续跑；资源采样 60s 间隔见 `build_resource.log`
  （本机构 RSS 峰值 ~8.9GB / 62GB = 14%）。
- 待补：ctest 结果（test_labspec / test_guided_workflow_widget / test_workbench /
  test_app，offscreen -j1）。

## Test gates (offscreen, CTEST_PARALLEL_LEVEL=1) — 2026-09-13

- 构建完成：`BUILD_DONE rc=0 targets=[test_guided_workflow_widget test_labspec sicnu_geo_rs]`；
  另建 4 个 workbench 套件。中途两处修复（base repair `55426a0797`、
  `Qt.AlignCenter`→`Qt::AlignCenter` `d51cc162`、PIMPL 补全 `df179ad`）。
- `./tests/test_guided_workflow_widget`（direct）：**All tests passed (60 assertions in 5 test cases)**
- `./tests/test_labspec`（direct）：**All tests passed (91 assertions in 5 test cases)**
  （含：shipped 清单 11 门加载、operator_id 全部可解析、params 全过 validateParameters、
  grading_ref 可解析、生成文档零 diff）
- ctest 形式（按 Catch2 用例名注册，仓库 catch_discover_tests 无 target 前缀，
  故 `-R` 用用例名正则）：
  `ctest -R 'Shipped lab inventory|Every lab operator_id|Every lab step|Every grading_ref|Generated lab documentation|WorkflowStep models|Valid LabSpecs|Invalid LabSpecs|Lab param paths|Shipped labs load' -j1`
  ⇒ **100% tests passed out of 10** (2.94s)
- 回归：`test_workbench_enum_provider` 5 cases ✓、`test_workbench_host` 8 ✓、
  `test_workbench_shutdown_policy` 4 ✓、`test_workbench_state_model` 6 ✓（全部 direct 运行）。
- `test_app` 家族在仓库中不存在（tests/CMakeLists.txt 323 个 add_executable 中无该前缀）；
  最接近的 app 侧证据 = `sicnu_geo_rs`（widget 所在 app target）链接成功 + 上述 workbench 套件。
- 注意：`ctest -R test_labspec`（target 名）在本仓库不匹配任何用例——注册名是 Catch2
  用例名（catch_discover_tests 无 TEST_PREFIX）。目标里的 `-R test_labspec` 按字面执行
  会得到 "No tests were found"；上面的用例名正则才是等价操作，证据如实记录。
