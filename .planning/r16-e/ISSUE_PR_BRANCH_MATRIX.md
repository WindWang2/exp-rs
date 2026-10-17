# R16-E Issue / PR / Branch Matrix（2026-10-17 现场重查）

来源：`gh pr list --state open`、`gh issue list --state open`、`git branch -r`、`git worktree list --porcelain`。

## 25 个开放 Issue 的处置分类

### A. 本任务范围内（T09/T10 直接负责）

| Issue | 标题摘要 | 初始分类 | 处置轨道 |
|---|---|---|---|
| #1489 | i18n：lupdate 覆盖不全、无 .ts 新鲜度门禁 | 部分修复，残留可修 | T09（.ts/audit 脚本）+ T10（CMakeLists lupdate 范围行，按 T09 规格执行） |
| #1465 | help/science_context 集群（terminology dedup 等 4×P2） | terminology 部分已修复；3-arg std::equal 在 science_state（他人归属，只读核实并记录） | T09 复核 + 回归 |
| #1491 | CI 无完成结果；test_io_vector_contract:352 | 待本地复现 :352；CI 阻断链已由 r13-i 修 | T10 本地复现/修复或给出"未复现"证据 |
| #1490 | concurrency 组替换排队运行 | **已修复**（SHA 分组，ci.yml:9-16） | 只在 PR 正文记录证据，不改代码 |
| #1494 | 孤儿测试 | **已修复**（已注册 + census 脚本） | T10 复核脚本覆盖面，必要时小增强 |
| #1495 | clang-tidy continue-on-error | **已修复**（阻断式步骤） | 记录证据 |
| #1468 | Windows _exit / fixture recv 超时 / netcdf 守卫 | **已修复**（三处均有代码） | T10 验证剩余未列项（如 http fixture）是否残留 |

### B. 范围外（他人归属——本 PR 不改，只在矩阵记录）

#1499（python worker 编译断裂——需验证 master 是否仍断裂，若本地编译通过则标记"已消失"）、#1488/#1447（json 类型安全，r13-k 归属）、#1486/#1462（python/plugins）、#1485/#1456（geospatial SSRF，#1513 归属）、#1467（core-fork）、#1466（teaching/suitability，r16-c 归属）、#1464（verify/grader，r16-a 归属）、#1463（agent_ops）、#1460（cli/sdk）、#1459（workflow/runtime，#1516 归属）、#1458（experiment，#1513 归属）、#1455（operators）、#1454（processing）、#1449/#1448（stores）。

## 5 个开放 PR

见 BASELINE.md 冲突矩阵。关键约束：#1517 拥有 `operator_help_provider.cpp` + `data/help/operators/{data_io,optical}.json` 的未合并改动；#1515/#1517 均追加式修改 `tests/CMakeLists.txt`。

## 22 个远程分支分类

- `origin/master`：基线。
- 已合并 PR 残留（r12/r13/r14 系列，8 条）：不动。
- 进行中 PR 头分支（5 条）：r15-A..E —— 只读。
- `origin/fix/all-open-issues`：历史修复分支，只读。

## 本地 worktree（17 个）

R16-A（integration/t01/t02）、R16-B（integration/t03/t04）、R16-C（integration/t05/t06）、`hardening/planner-repair-capability-graph`、`hardening/verifier-grader-explain-evidence`、`agent/*` 3 个 —— 全部只读。R16-E 新增 3 个（见 BASELINE.md）。
