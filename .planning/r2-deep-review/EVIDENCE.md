# EVIDENCE — r2-deep-review

## E-001 · 构建工具链不可用（Phase 0）

- `which cmake ninja gcc cl` → 全部空；`cmake --version` → command not found。
- 结论：本 track 构建/测试验证全部标 not-executed（GOAL 已声明）。

## E-002 · 子代理执行记录

- 子代理 B（领域数据区）：成功，17 条候选（含正面确认），~3M tokens。
- 子代理 A（机械扫描区）：首次 `Model request failed`；后台重试 `captcha verify failed`。
  按 GOAL 默认主线内联接管（DECISIONS D-001）。

## E-003 · 去重 API 限流记录

- `gh api search/issues` 逐关键词查询：第 1–2 个返回（class_mapping → 0、
  srcCrsOverride → 0），第 3 个起停滞；后台任务已停止。
- 改用本地去重三支柱（DECISIONS D-002）。

## E-004 · issue 提交记录（Phase 5）

- #959 F2-01（help catalog i18n，P1）· #960 F2-02（offline gate 竞态，P1）·
  #961 F2-03（setenv MSVC，P1）· #962 F2-04（class_mapping，P2）·
  #963 F2-05（srcCrsOverride，P1）· #964 F2-06（band-role 波长，P2）·
  #965 F2-07（波段序假设，P2）· #966 F2-08（溯源未落地，P2）·
  #967 F2-09（Gaussian 单调，P2）· #968 F2-10（teacher 剥离，P2）·
  #969 F2-11（ADR 0146×9，P3）· #970 F2-12（白名单，P3）· #971 F2-13（NMS，P2）
- 验证：13 条 body 全部含 `## Location` 节（`gh issue view <n> --json body` 逐条 grep = 1）。
- 标签：`needs-triage`、`severity:P1/P2/P3` 新建；类型标签沿用 `bug`/`documentation`。

## E-005 · 存在性断言（runbook 第 5 步）

- dossier 引用的 22 个文件:行号全部 `test -e` + `sed -n` 行内容复验通过
  （Phase 4 两批输出，无 MISSING）。
- `git diff --name-only origin/master...HEAD | grep -cE "^(src|tests)/"` → 0（PR 前复核）。

## OUT_OF_SCOPE

- （无 P0 范围外发现。）
