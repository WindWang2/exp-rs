# GOAL — R0 · Whole-Repository Line-by-Line Review

- **Track branch**: `zcode/whole-repo-line-review` @ worktree `/home/kevin/projects/rs-studio/exp-rs-whole-repo-line-review`
- **Mode**: unattended long-running epic, review-only. `src/` + `tests/` read-only. Write scope: `review/`, `WHOLE_REPO_REVIEW.md`, `docs/`, `.planning/`.
- **Budget**: 300,000,000 tokens total, phase allocation in PLAN.md. Subagents ≤ 2 (Phase 7 only, read-only).
- **No CI.** Local evidence only → EVIDENCE.md. No remote issue creation (`gh issue create` forbidden).
- **Hard gates**: verbatim code quotes for every finding; repro (Catch2 draft or local command) for every P0/P1; zero duplicates vs the 250 closed issues (#595–#945) + historical audit findings; Tier A coverage 100% `reviewed`; PR diff contains no `src/`/`tests/` changes.
- **Six lenses** (every area): 1 科学计算与算法契约, 2 生命周期与内存安全, 3 并发与状态同步, 4 求解器与语义, 5 契约/错误/元数据漂移, 6 测试可信度.
- **Severity vocabulary** (repo-existing): P0 崩溃/内存破坏/科学结论错误/数据静默损坏/凭据泄漏; P1 未处理边界/竞态/泄漏/契约违背; P2 性能/非确定性/错误类型不一致/缺失防御; P3 代码债/文档脱节.
- Full original GOAL text: provided by user at track start; operating envelope and finding format are binding.
