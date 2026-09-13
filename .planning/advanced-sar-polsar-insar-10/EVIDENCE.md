# EVIDENCE — advanced-sar-polsar-insar-10

证据政策：每条能力断言映射到本地命令 + 退出码，或显式标注 `not-executed`。

## Phase 0 — 基线与考古（2026-09-13）

| 事实 | 验证命令 | 结果 |
| --- | --- | --- |
| origin/master SHA | `git rev-parse origin/master` | `7d78059d1a6d316d606656759a506d17bc5e3b55` |
| 无 SAR 在飞分支 | `git branch -a \| grep -i sar` | 空 |
| 无 SAR worktree 残留 | `git worktree list` | 14 个，无 SAR |
| 近 40 PR 全 MERGED，无 SAR 平台 track 在飞 | `gh pr list --state all --limit 40` | #958–#889 全部 MERGED |
| SAR 相关 issue 全部 CLOSED | `gh issue list --search "SAR..."` | #854/#855/#929/#785/#803/#934/#330 CLOSED |
| planning 可跟踪 | `git status --porcelain` | `?? .planning/advanced-sar-polsar-insar-10/` |
| .git/info/exclude 修复 | `sed -i 's|^\.planning/$\|.planning/*\|'` | `.planning/*`（与 .gitignore 模式一致） |

### 去重关键结论

1. `rs:sar_temporal_stats` 波段序是已发布契约（sar-domain.md §5 `SICNU_SAR_TEMPORAL_BANDS`），
   S-1 修复必须走新算子，不改既有波段序（DECISIONS D-007）。
2. 轨道/zero-Doppler/forward-RD/真几何 gamma0 已由 Scientific Algorithms 7.0/8.0 交付
   （sar-domain.md §3-§4），本 Track 不得重做 geocoding；InSAR 链的 geocoding 步骤复用
   `rs:sar_geocode`。
3. capability sidecar 走 ADR 0146 gen-meta 流程，`pi/knowledge/capability-sar.md` 是生成
   产物禁止手编。

### OUT_OF_SCOPE（范围外发现）

（暂无；持续记录）

## 预算节（每 Phase 结束追加）

| Phase | 时间戳 | 工具调用数 | 触及文件数 |
| --- | --- | --- | --- |
| 0 | 2026-09-13 | ~20 | ~15 (读) |
