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
3. capability sidecar 走 ADR 0154 gen-meta 流程，`pi/knowledge/capability-sar.md` 是生成
   产物禁止手编。

### OUT_OF_SCOPE（范围外发现）

（暂无；持续记录）

## 预算节（每 Phase 结束追加）

| Phase | 时间戳 | 工具调用数 | 触及文件数 |
| --- | --- | --- | --- |
| 0 | 2026-09-13 | ~20 | ~15 (读) |
| 2-5 (WP-A..D 落盘) | 2026-09-13 深夜 | ~60 | 新增 30（内核 10 + 算子 14 + 测试 5 + CMake/docs） |

## 并发 Track 观察

- `exp-rs-scientific-contract-verification-10` worktree @ 同 baseline 正在并行构建
  （sicnu_geospatial/sicnu_operators/sicnu_contracts，-j2）。共享热点监控中；
  目前无同文件触碰。我方 -j2 + 对方 -j2 ≤ 16 核预算。
- 发现并修复的编译缺陷：`sar_complex.h` 中 `class GdalStreamingOutput&` 写在
  namespace 内导致声明了 `sicnu::sar::GdalStreamingOutput` 独立类型
  （build2 失败原文见 /tmp/sar10-build2.log）；前向声明移到全局作用域修复。

## OUT_OF_SCOPE（范围外发现）

（暂无；持续记录）

## Phase 8 — 最终本地验证（最终 HEAD `2d19055`，rebase 后无漂移）

`git fetch origin && git rebase origin/master` → Already up to date（origin/master 仍为 7d78059）。
`git diff --check` exit 0；conflict-marker 扫描 0 命中；secret 扫描 0 命中（唯一命中为
`parsePolChannel(const QString &token, ...)` 的通道 token 参数名，非凭据）。

### 测试矩阵（QT_QPA_PLATFORM=offscreen，逐二进制直跑，最终 HEAD）

| 套件 | 结果 |
| --- | --- |
| test_sar_complex（新） | All tests passed (378 assertions / 7 cases) |
| test_sar_polsar（新） | All tests passed (143 / 8) |
| test_sar_insar（新） | All tests passed (266 / 10) |
| test_sar_temporal_events（新） | All tests passed (90 / 5) |
| test_sar_platform10（新 E2E） | All tests passed (2766 / 11) |
| test_sar_temporal_stats（回归） | All tests passed (238 / 5) |
| test_sar_orbit（回归+新基线测试） | All tests passed (57 / 8) |
| test_sar_kernels（回归） | All tests passed (89 / 12) |
| test_sar_operators（回归） | All tests passed (595 / 19) |
| test_sar_geocoding（回归） | All tests passed (5769 / 8) |
| test_sar_foundation5（回归） | All tests passed (279 / 6) |
| test_capability_contract_9 | All tests passed (2013 / 5) |
| test_contract_platform_9（快照重生成后） | All tests passed (15 / 4) |

### 生成物校验

- `capability_knowledge_tool gen-meta` → wrote 121 sidecars, 0 problems
- `gen-pages` → written；`gen-pages --check` → **zero diff**
- `contract_inventory` → nodes 873 / edges 279 / findings 0（快照已随 schema 演进再生成并提交）

### 预存在失败（master @ 7d78059 即失败，OUT_OF_SCOPE，证据见 REVIEW_LOG 守卫节）

- test_capability_drift：cartography tools ×3、rs:gaofen/zy3/hj_import 知识条目、recipe 别名 canary
- test_mapspec：制图视觉用例 SIGABRT（classification-a4l；与本 track 零文件交集）

### 存在性断言（runbook 第 6 步）

- 技能路径断言：输出为空（全部存在）✓
- docs/review/.planning 引用断言：输出为空（全部存在）✓
- `git ls-files .planning/advanced-sar-polsar-insar-10/ | wc -l` → 10（planning 全部被跟踪）
- `git log --oneline origin/master..HEAD | wc -l` → 8 commits（含独立 integration commit）

### 预算节（续）

| Phase | 时间戳 | 工具调用数 | 触及文件数 |
| --- | --- | --- | --- |
| 6（sidecar/knowledge/守卫） | 2026-09-14 凌晨 | ~25 | ~15 |
| 7-8（review 两轮 + 修复 + 最终验证） | 2026-09-14 | ~45 | ~20 |
