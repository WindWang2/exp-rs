# R25-B PR_COLLISION_MATRIX

- 执行时 open PR：**0**（2026-10-28 14:03 UTC 实查）。无写冲突。
- 并行风险=兄弟轨 R25-A/R25-C **同宿主 worktree**（非 PR）：共享构建锁（flock）与 CPU/内存，不共享文件写区。本轨写区与 A（src/operators/rs、src/analysis）、C（src/agent 等）、D（src/app）、E（src/lab）无交集。
- `tests/CMakeLists.txt`：本轨只追加末尾 `# BEGIN R25-B` 块（append-only），不动他人块；合并守卫 scripts/check_merge_registration_integrity.py 验证并集。
- `.gitignore`：仅在文件末尾追加本轨 allow-list 块（与 r23/r24 各轨同模式）。
- push 前重新 fetch 复核（ORACLES O-8）。
