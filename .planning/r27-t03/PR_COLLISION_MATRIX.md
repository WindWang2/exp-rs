# PR 冲突矩阵（2026-11-02T13:24Z）

- origin/master = 10652fd003（= base），其后无任何新 PR/分支。
- 兄弟轨 worktree（r25-A..E, r26-*）均为已合并 PR 的遗留 checkout，无未合并提交，不触碰。
- tests/CMakeLists.txt：本轨只做 append-only 追加块（唯一标识 `# ── R27-T3 ──`），提 PR 前三方核对。
- 共享保护文件（根 CMakeLists.txt、.gitignore、docs/adr/**）：本轨零改动。
