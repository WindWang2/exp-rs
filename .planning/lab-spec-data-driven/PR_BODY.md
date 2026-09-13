# PR Body — feat(lab): data-driven LabSpec + operator-bound guided workflows

## Summary

今天"本科实验有几门"有三个互相矛盾的答案：`docs/labs/` 手写 7 门、
`guided_workflow_widget.cpp` 硬编码 10 门、文档引用的菜单路径在 ADR 0099 改版后已消失。
本 track 引入 **LabSpec**（实验声明式规格，`data/labs/*.lab.json` +
`data/schemas/labspec.schema.json`）：一份 JSON 驱动 UI 引导、headless 执行与自动判分接口，
实验文档变为构建产物（`scripts/gen_lab_docs.py`），三个实验清单合并为一个规范集（11 门）。

## Changes

- **A** `data/schemas/labspec.schema.json`（draft-07）：`{operator_id, params}` 与 UI 动词
  `action` 互斥、`params` 依赖 `operator_id`、id↔文件名一致、`grading_ref` 指向既有判分管线。
- **B** 新 `src/app/widgets/lab_spec_loader.{h,cpp}`：纯 QtCore+jsoncpp 的加载与结构校验，
  类型化 `LabSpecError`（无异常、无静默回退），目录加载顺序确定；
  `GuidedWorkflowWidget` 改为从 `data/labs/` 渲染，加载失败以错误条目置顶呈现，
  **10 个 `createXxxWorkflow()` 工厂删除**。
- **C** operator-bound steps：widget 通过与对话框/agent 相同的
  `JobRequest{algorithmId, params} → GuiJobHandle` 路径执行算子步骤（与 headless 同一条
  执行路径）；`data/…`/`outputs/…` 路径在提交边界经注入式 resolver 解析。
- **D** `scripts/gen_lab_docs.py`：确定性渲染 `docs/labs/<id>.md` + `README.md`，
  `--check` 提供零 diff 门（旧 7 篇手写文档由生成产物替代）。
- **E** 规范实验集：docs 7 门 ∪ 硬编码 10 门 = **11 门**（lab01–lab11，PCA/Mosaic/OBIA
  升为编号实验而非删除）。
- **F** `tests/test_labspec.cpp` drift guards：operator_id 在 Processing Registry 可解析、
  params 过 `validateParameters`（与 agent tool-call 同一校验 seam）、grading_ref 可解析、
  生成文档零 diff。
- **G** `tests/test_guided_workflow_widget.cpp` 从结构体字段往返重写为行为测试
  （合法/非法加载、互斥规则、路径解析、shipped 清单 ≥10）。
- **H** `docs/labs/LABSPEC.md`（创作指南）+ ADR 0146（契约决策记录）。

## Verification (local only — this repo's gates are local)

- `test_labspec`：5 cases / 91 assertions 全绿（含 shipped 11 门加载、operator_id 全部
  在 Processing Registry 解析、params 全过 `validateParameters`、grading_ref 可解析、
  生成文档零 diff）。
- `test_guided_workflow_widget`：5 cases / 60 assertions 全绿（行为化重写后的
  合法/非法加载、互斥规则、路径解析、清单一致性）。
- ctest 汇总（Catch2 用例名注册，`-R` 用用例名正则）：**100% tests passed out of 10**。
- 回归：`test_workbench_{enum_provider,host,shutdown_policy,state_model}` 4 套件 23 cases
  全绿；`sicnu_geo_rs`（widget 所在 app target）链接成功。
- `python3 scripts/gen_lab_docs.py --check` 零 diff。
- 构建全程 `-j1~-j2` / `CTEST_PARALLEL_LEVEL=1`，60s 资源采样见 `EVIDENCE.md`；
  offscreen 运行。完整证据：`.planning/lab-spec-data-driven/EVIDENCE.md`。

## Notes

- `master` 未动；分支自 `origin/master@27b9aa0a63` 切出，PR 前已 rebase。
- diff 限于 data/labs、data/schemas、src/app/widgets、tests、docs/labs、scripts、
  .gitignore，外加两行机械必需的 CMakeLists 变更（新 loader 文件接入构建）。
- 决策记录：`.planning/lab-spec-data-driven/DECISIONS.md`。
