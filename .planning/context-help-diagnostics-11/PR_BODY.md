# PR BODY — F20 context-help-diagnostics-11

（Phase 8 完成后定稿；此为骨架，逐节填写实际内容）

## Baseline

- origin/master @ `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- 本 track 分支：`zcode/context-help-diagnostics-11`，worktree `../exp-rs-context-help-diagnostics-11`

## Dedupe / 并行 ownership

- #1009（execution-11，open）：其 diagnostics.json 变更 = 文件尾追加 2 个 operator 页；本 track 的新增页全部位于文件中段 harness 区之后（DECISIONS D1），无文本冲突；未复制/重做其功能。
- #1008（spectral，open，CONFLICTING）：与 primary scope 零交集。
- Issues #1001–#1007：dataset/workflow/io/georef 域 bug，不属本 track（EVIDENCE OUT_OF_SCOPE）。

## 实际交付

（按 WP A–H 填写）

## 架构决定

（引用 DECISIONS.md D1–D12 摘要）

## 兼容性

（schema 变更、API 变更、行为变更清单）

## Local tests / evidence

（TEST_MATRIX 终态表 + 两次连续运行记录；Local evidence only; no online CI dependency）

## Review findings

（REVIEW_LOG 摘要：P0=0 P1=0 及 disposition）

## Known limitations / follow-ups

（OUT_OF_SCOPE 清单）
