# GOAL — D2 · LabSpec Data-Driven Labs

> Source: D2 goal directive (2026-09-12). Track branch `zcode/lab-spec-data-driven`,
> worktree `/home/kevin/projects/rs-studio/exp-rs-lab-spec-data-driven`.

## Mission

把实验从 C++ 硬编码与手写 Markdown 里解放出来：一份 JSON（LabSpec）→ 驱动 UI 引导、
驱动 headless 执行、驱动自动判分（D4/D7 接口）、生成实验文档。
**让文档成为产物，而不是第二份真相。**

## Non-negotiables

- Autonomy: fully unattended；决策记入 `DECISIONS.md`。
- Subagents ≤ 2，只读（审查/验证用）。
- No CI — 本地 build/test/bench 证据入 `EVIDENCE.md`。
- `master` 只读；worktree + branch 先行。
- 构建资源硬约束: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, Ninja `-j2`
  （RSS>70% 或 load>1.5×cores 时 `-j1`）；每 60s 记录 CPU/RSS。
- 测试: `QT_QPA_PLATFORM=offscreen`；先 targeted `ctest -R <family> -j1`。
- 退出: PR 创建即停，不 merge，不等 checks。

## Autonomy defaults（已采纳，不再询问）

1. Format = JSON + JSON Schema（repo 已有 `data/schemas/pipeline_schema.json`）。
2. Schema: `data/schemas/labspec.schema.json`；labs: `data/labs/<lab_id>.lab.json`。
3. `.gitignore` 加 `!data/labs/` + `!data/labs/**`。
4. 无 C++ fallback 内容；缺失/非法 LabSpec = 类型化错误进 UI；达到 parity 后删除
   10 个 `createXxxWorkflow()` 工厂。
5. `actionId` 语义升级为 `{operator_id, params}`；执行路径与 headless 同一
   （`JobRequest` → TaskCenter，同 dialogs 的 `runOperatorTask` seam）。
6. 文档是产物：`scripts/gen_lab_docs.py` 生成 `docs/labs/*.md`；markdown 不可手改。
7. 实验清单 = 7 文档 + 10 硬编码的并集去重（≥10），孤儿（PCA/Mosaic/OBIA）升为编号实验。
8. 新 UI 字符串用 `tr()`；不做全库翻译抽取（那是 D6）。

## Completion gate（摘要）

- `data/labs/` 被 track（`git check-ignore -v` 验证）。
- 所有 LabSpec 过 schema 校验；`operator_id` 在 Processing Registry 可解析；参数名匹配 operator schema。
- `GuidedWorkflowWidget` 全部由 JSON 渲染；10 个 `createXxxWorkflow()` 消失。
- `scripts/gen_lab_docs.py` 重生成 `docs/labs/` = 零 diff。
- `ctest -R test_labspec`、`ctest -R test_guided_workflow_widget` 绿（offscreen）；`test_workbench`/`test_app` 无回归。
- 三个清单（docs/widget/LabSpec）实验数一致。
- Review P0=P1=0；处置记入 `REVIEW_LOG.md`。
- Diff 限于 `data/labs/` `data/schemas/` `src/app/widgets/` `tests/` `docs/labs/` `scripts/`
  （+ 机械必需的 CMakeLists 行，见 DECISIONS）。
