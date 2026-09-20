# OWNERSHIP — Track `ds41-fuzz-boundaries`

## 可写（主 owner 范围）

### 新增文件（本 Track 创建）

| 路径 | 内容 |
|---|---|
| `tests/support/fuzz_corpus.h` | 结构化 mutator + delta-debugging 最小化器 + corpus 记录器（test-only，include-only） |
| `tests/test_contract_fuzz_frame.cpp` | IPC framing（`exprs::IpcFrame`）+ envelope（`exprs::Ipc`）bounded property lane |
| `tests/test_contract_fuzz_payload.cpp` | JSON 契约载荷 lane：workflow schema、UI schema、diagnostics、manifest 相邻面、path policy |
| `tests/test_contract_fuzz_paths.cpp` | 路径 containment/遍历/Unicode lane（std::filesystem temp 目录，hermetic） |
| `tests/corpus/**` | 最小化 fixture + root-cause 记录（若发现 defect） |
| `.planning/ds41-fuzz-boundaries/*.md` | Track 规划账本（markdown only） |
| `.goal-loop-ledger.md` | GOAL Loop 本地账本（worktree-local，不提交，除非仓库规范要求；见 DECISIONS） |
| `PR_BODY.md` | PR body 草稿（worktree-local；最终内容由 `gh pr create` 提交） |

### append-only 修改（共享文件）

| 文件 | 修改方式 |
|---|---|
| `tests/CMakeLists.txt` | **仅在文件末尾追加**新的 fuzz target 定义（沿用 7.0/8.0 "task D" 块的既有约定与注释风格）。不改动任何既有 target 定义。 |
| `.gitignore` | **仅在末尾追加**`.planning/ds41-fuzz-boundaries/` 白名单三行（照抄既有 track 的写法）。 |

### 生产源码（例外条款内才写）

`src/**` 默认只读。例外：本 Track fuzz 证明出的 **P0/P1 缺陷**（必须修）；被证明的 P2
缺陷在修复同样极小、无并行 owner、且不改变合法输入行为时也一并修。每个修复遵循：
类型检查先于转换、try/catch 只包住不可信输入解析、诊断信息形状不变。若修复超出
"极小"范围 → 开 issue + 保留 minimized corpus，不进本 PR。

## 只读

- `src/**`（除上述例外条款）
- `tests/test_exprs_ipc.cpp`、`tests/test_exprs_workflow_schema.cpp`、`tests/test_plugin_ui_schema*.cpp`（既有已知答案腿，只读引用）
- `tests/support/bounded_fuzz.h`（既有共享生成器；本 Track 的新 mutator 放新文件，不改它，避免与并行 Track 冲突）
- `docs/**`、`CMakeLists.txt`、`src/**/CMakeLists.txt`、`.gitignore`（追加除外）
- `.planning/<other-track>/**`

## 冲突回避策略

1. 若构建时发现 `tests/CMakeLists.txt` 已被并行 PR 修改（`git rebase origin/master` 后出现冲突）：
   只解决本 Track 末尾追加块的冲突（保留双方追加），不做语义合并。
2. 若发现并行 PR 修改了 `src/sdk/exprs/**`：rebase 后重跑本 Track 的 oracle；
   若并行PR 已修同一缺陷 → 删掉本 Track 的重复修复，改为在其上补 regression（不复制实现）。
3. 本 Track 不往日常全量测试引入长跑路径：所有新 target 有硬性 iteration cap 与
   size cap（≤512B 输入 / ≤600 次迭代 / seed），并在 CMake 注释中声明 smoke 时长。
