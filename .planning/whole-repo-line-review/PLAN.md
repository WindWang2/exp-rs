# PLAN — execution phases

| Phase | Content | Budget (M tok) | Status |
|---|---|---|---|
| 0 | 分区清单 + COVERAGE_LEDGER 骨架 + 去重基线（250 issues + .scratch/audit-final + findings-audit.md） | 18 | done |
| 1 | A 高风险核心: operators DONE(4F) / processing (task_center 深读, 其余结构过) / agent (结构过) | 48 | in_progress |
| 2 | B 应用外壳: src/app — 生命周期核心验证 + 风险扫 + delta hunk；519 文件结构过 | 54 | done |
| 3 | C 基础层: sdk 深读(ipc/registry) DONE; geospatial(util/atomic 深读) 其余风险扫; data/analysis 见 Phase 6 后收尾扫 | 46 | in_progress |
| 4 | D 治理与运行时: jobs/workflow 深读 DONE; E pi 深读 DONE (2F); D 其余见收尾扫 | 38 | in_progress |
| 5 | F 测试套件可信度: 透镜6 扫描完成, 无新发现 (#656 族已裁决) | 34 | done |
| 6 | G ANTIGRAVITY 3 文件深读(无第一方调用方,干净) + delta-pass 23 commits DONE; H recipe/schemas/drift-gate 抽验 DONE; src/ui 551 行已入账 | 24 | done |
| 7 | 交叉复核: V 6/6 KEPT (2 证据精化); C 审计→F-OPS-5 补档+计数修复+方法笔记补全 | 20 | done |
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
