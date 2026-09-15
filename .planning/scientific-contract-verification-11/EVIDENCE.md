# EVIDENCE — scientific-contract-verification-11

所有 capability claim 必须对应本地命令 + exit code。持续追加。

## Phase 0

- `git fetch origin --prune` → ok。`git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list` → 2 open（#1008 CONFLICTING、#1009 MERGEABLE）；`gh issue list` → 7 open（#1001–#1007）。逐条 dedupe 见 BASELINE.md。
- Subagent #1（Explore，只读）完成 verification/contract 体系审计：115 rs: contracts全覆盖、~25 非 rs: 算子无 contract、determinismGrade override 仅 46 处、metamorphic 1 条、L3–L7 证据缺口官方记录。见 BASELINE.md §Verification 10.0。
- Worktree 创建：`git worktree add ../exp-rs-scientific-contract-verification-11 -b zcode/scientific-contract-verification-11 origin/master` → HEAD `a5b11b7f`。
- 本机构建环境验证：5 个 sibling worktree 的 build-dev 均有 CMakeCache（Ninja/MSVC/Qt6.8/vcpkg 配方成立）；`C:\deps\{Qt,qca-install,kc-install,vcpkg,catch2-src}` 存在。

## Host limitations（一次性记录）

- load average 在 Git Bash/Windows 不可测 → 按 GOAL 记 not-executed，build 恒定 `-j2` 上限（RSS>70% 时降 `-j1`，用 PowerShell 抽样判断）。
- CPU/RSS 每 60s 用 PowerShell `Get-Process`/`tasklist` 抽样，日志入 build-dev/monitor.log（本地，不入库）。

## OUT_OF_SCOPE

- **#1001 io:clip srcCrsOverride→targetCrs silent wrong clip（critical/P1）**：master 未修；修复点 `src/operators/io`（本 track read-only）。已入 FAILURE_MATRIX + PR_BODY 顶部 P0 通告。
- **#1005 mapPickToLayerCrs 未变换点返回（critical/P1）**：同上，georef 交互层。
- **#1002/#1003/#1004/#1006/#1007** fail-open 类：workflow/dataset/agent 区（#1009 冲突区 / 他人 own），follow-up。
- 7 条均为 fail-open/静默语义缺陷 —— 印证本 track failure-semantics contract 的必要性；机制性防御属本 track，点修属 follow-up。

## Build log

（configure/build/monitor 结果追加于此）
