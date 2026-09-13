# PLAN — execution phases

| Phase | Content | Budget (M tok) | Status |
|---|---|---|---|
| 0 | 分区清单 + COVERAGE_LEDGER 骨架 + 去重基线（250 issues + .scratch/audit-final + findings-audit.md） | 18 | done |
| 1 | A 高风险核心: src/operators (260f/54k), src/processing (409/61k), src/agent (146/53k) | 48 | in_progress |
| 2 | B 应用外壳: src/app (519/115k) | 54 | pending |
| 3 | C 基础层: geospatial, data, analysis, sdk (288/65k) | 46 | pending |
| 4 | D 治理与运行时 (dataset/experiment/workflow/cli/python/help/contracts/runtime/jobs/native/stubs) + E plugins/pi | 38 | pending |
| 5 | F 测试套件可信度: tests/ (501/174k) 透镜 6 | 34 | pending |
| 6 | G vendored 局部改动 (ANTIGRAVITY 标记) + 集成面 + H 资源与契约漂移 (.ui/.qss/.qrc/data json/schemas) | 24 | pending |
| 7 | 交叉复核: subagent V (假阳性清扫) + subagent C (coverage 审计); 主代理裁决 | 20 | pending |
| 8 | I 去重汇总 DEDUPE.md + 执行摘要 WHOLE_REPO_REVIEW.md + issue 草稿 + PR | 18 | pending |

## Method per area
1. File inventory from `git ls-files` → ledger rows.
2. Read every first-party file in the area (batched `sed -n`/Read), all six lenses.
3. Findings appended to `review/findings/<area>.md` in the required format (verbatim quotes only).
4. Ledger updated immediately per file batch (resumable), commit `review(<area>):` per area.
5. P0/P1: write `review/tests/<id>.cpp` Catch2 draft or a local command; verification builds only when executed (-j2, offscreen), logged to EVIDENCE.md.

## Progress checkpoint format (for context compression)
State is durable in COVERAGE_LEDGER.csv + findings files. On resume: read PLAN.md status column, find first non-done phase, resume from ledger's first non-`reviewed` file in that area.

## Budget watchdog
Report if any phase exceeds 1.5× its budget. Token spend is not directly meterable; proxy = tool-call volume and context turns per phase, tracked in REVIEW_LOG.md.
