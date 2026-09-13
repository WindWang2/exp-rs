# GOAL — advanced-sar-polsar-insar-10 · Advanced SAR / PolSAR / InSAR Scientific Platform 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/advanced-sar-polsar-insar-10` (worktree `../exp-rs-advanced-sar-polsar-insar-10`, off `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on, trigger, or cite online CI.
> **Write scope:** `src/processing/algorithms/sar/`, `src/operators/rs/rs_sar_*`, `src/operators/rs/rs_operators_init.cpp` (append-only), `src/operators/CMakeLists.txt` (append-only), `tests/` (new SAR tests + test CMake append), `docs/processing/sar-domain.md` (append), `data/processing/algorithm_meta/` (SAR sidecars), `pi/knowledge/` (regenerated SAR pages only), `.gitignore` (one whitelist block), `.planning/advanced-sar-polsar-insar-10/`
> **Read-only:** `master` branch everywhere; `src/processing/gdal/` (raster IO authority — reuse, don't fork); `src/geospatial/` (CRS/metadata authority); product-format import adapters (Track 02 ownership: `rs_*_import_operator.*`, `src/geospatial/products/`); temporal optical family `rs:temporal_*` (fusion seam = interface only); everything else outside the write scope.

## Mission

现状：平台已有 12 个 SAR 算子（`src/operators/rs/rs_sar_*.cpp`，注册于 `rs_operators_init.cpp:129-158`）+ 10 个内核模块（`src/processing/algorithms/sar/`），覆盖定标（sigma0/gamma0/beta0，linear/dB 域契约 `sar_metadata.h`）、双极化特征（`sar_dualpol.h`，诚实声明非 quad-pol）、斑点滤波、bi-temporal 比值/变化（`sar_ratio.h`）、纹理、常几何地形掩膜（`sar_terrain_geometry.h`，#785 look-azimuth 修复）、轨道状态向量 + zero-Doppler 定位 + forward range-Doppler geocoding（`sar_orbit.h`/`sar_geocoding.h`，`rs:sar_geocode`）、N 景时序统计（`sar_temporal.h`）。这些都是已合并、有已知答案测试的能力，**不得重做**。

真问题：(1) `rs:sar_temporal_stats` 的 `argmax_date` 是 0-based 场景索引、无日历日期语义（ISSUES.md S-1，`sar_temporal.h:42`）——多时相 SAR 结果无法回答"何时发生"；(2) 无任何极化分解能力（ISSUES.md S-2）——只有 dual-pol 特征，没有 complex channel 模型、Pauli/H-A-α/Freeman-Durden/Yamaguchi，全极化实验课无法开设；(3) 栅格栈把 complex（SLC）数据当 float 处理——相位语义在进入内核前就丢失；(4) 无 InSAR 基础链——干涉图/相干性/相位滤波/解缠/形变全部缺失。

做完之后：平台具备**可执行的 PolSAR 核心链**（complex 通道契约 → 协方差/相干性矩阵 → 三种分解 + 已知答案测试）、**InSAR 基础链**（SLC 配对契约 → 干涉图+相干性 → 相位滤波 → 解缠（参考实现或 typed provider contract）→ LOS 形变 → geocoding 复用）、**真实时间语义的多时相 SAR**（acquisition dates → event dating / trajectory / anomaly，index↔date 映射进结果 JSON），全部 bounded-memory、可取消、bit-exact/tolerance 确定性分级、经 Agent capability metadata 表达 prerequisites 与 limitations。dual-pol 与 full-pol、linear 与 dB、SLC 与 detected 的 modality/domain 契约全程不混淆；近似算法不冒充严格算法（延续 sar_terrain_geometry.h 的 honesty-rule 传统）。

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the default in Autonomy defaults; record every taken decision in `.planning/advanced-sar-polsar-insar-10/DECISIONS.md`.
- **Precedence**: this GOAL overrides `.agents/AGENTS.md` / `CLAUDE.md` on conflict. AGENTS.md §1's "surface options / clarify" duty is discharged by writing options and the taken default into DECISIONS.md.
- **Agent**: `zcode`. **Token budget: 300,000,000**. When a phase exceeds 1.5× its allocation: append a budget line (phase, clock time, commands run, files touched) to EVIDENCE.md and continue — never stall silently, never ask.
- **Subagents: at most 2**, both read-only（#1 = Phase 7 架构 + 科学/语义正确性审查；#2 = Phase 7 性能/并发/生命周期/测试可信度审查）。Main agent owns 100% of implementation and judgment. Subagents spawn no further subagents. A subagent that fails or returns nothing usable: the main agent performs that review inline, records the failure in EVIDENCE.md, and does not spend an extra slot.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only → `.planning/advanced-sar-polsar-insar-10/EVIDENCE.md`. Every capability claim maps to a local command + exit code, or is explicitly marked not-executed.
- **Branching**: `master` is read-only. Worktree + branch already created off `origin/master` @ `7d78059d1a`.
- **Build entry**: `cmake --preset build-dev` (record configure exit code once in EVIDENCE.md). `build.cmd` / `configure_*.cmd` carry stale absolute paths — do not follow them.
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`; Ninja `-j2`, drop to `-j1` when RSS > 70% or load > 1.5× cores; `-j$(nproc)` is forbidden. Measure with `ps`/`uptime` (Linux host). Log CPU/RSS every 60 s during long builds.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` before any broad run.
- **Exit**: PR created, not merged; worktree retained until merge, then removed.

## Autonomy defaults (do not ask — apply these)

1. **格式/来源**：PolSAR/InSAR 输入采用平台既有 GDAL 栅格栈 + additive dataset-metadata contract（延续 `docs/processing/sar-domain.md` §3 的 SICNU_SAR_* 键模式）；SLC = CFloat32 波段（GDAL 原生支持），通道身份以 metadata 键 + 显式波段映射参数双重表达；产品格式解析（SAFE/CEOS 等）不实现——Track 02 ownership。
2. **失败项处置**：contract 缺失/矛盾 → typed refusal（错误码 + 补救提示），绝不近似；单个测试 fixture 失败 → 修复或标记，不跳过整个 suite；不可解的数学边界（如相位解缠 ≥π 梯度）→ 输出 NaN + 结果 JSON 计数，绝不伪造。
3. **命名/编号**：内核 `sar_<topic>.{h,cpp}` in `src/processing/algorithms/sar/`；算子 `rs_sar_<topic>_operator.{h,cpp}` in `src/operators/rs/`；注册 id `rs:sar_<topic>`；metadata 键 `SICNU_SAR_<TOPIC>`；错误码全大写下划线，沿用 `POLARIZATION_MISMATCH` 风格。冲突时 grep 确认无既有同名再采用。
4. **资源与超时**：单命令超时 10 min（build 链接 20 min）；超时降 `-j1` 重试一次，再超时记录 EVIDENCE.md 并继续下一项。
5. **对外动作**：`git fetch`（只读）允许；`git push` 仅本 track 分支；`gh` 仅用于 `pr create` 与只读查询；不评论、不 merge、不改 issue。
6. **范围外发现**：记入 EVIDENCE.md `OUT_OF_SCOPE` 节；P0 级另在 PR_BODY.md 顶部 `P0 (out of scope)` 标注；不在本 track 修复。
7. **依赖新增**：禁止。3×3 Hermitian 特征分解、复数窗口 IO、相位解缠参考实现全部本仓库内实现（C++20 标准库 + GDAL；不引入新依赖）。

## Current state & evidence (verified)

| Fact | Evidence |
| --- | --- |
| origin/master @ `7d78059d1a`，clean | `git rev-parse origin/master` → `7d78059d1a6d316d606656759a506d17bc5e3b55` |
| 12 个 SAR 算子已注册 | `rs_operators_init.cpp:129-158,275-286` |
| 10+ 个 SAR 内核模块 | `ls src/processing/algorithms/sar/` → calibration/dualpol/geocoding/metadata/orbit/ratio/speckle/temporal/terrain/terrain_geometry/texture |
| argmax_date 无日期语义（S-1） | `sar_temporal.h:41-42`（`argminDate/argmaxDate` 0-based index）+ ISSUES.md S-1 |
| 无 PolSAR 分解（S-2） | `grep -rn "Freeman\|Cloude\|Pauli\|polarimetric" src/` → 0 命中；ISSUES.md S-2 |
| 无 complex 数据路径 | `grep -rn "CFloat" src/processing/algorithms/sar/ src/operators/rs/` → 0 命中；GdalMultibandBlockStream 仅 float BIP（`gdal_multiband_block_stream.h:91`） |
| 轨道/zero-Doppler/forward-RD 已实现 | `sar_orbit.h:89-104`；docs/processing/sar-domain.md §3-§4 |
| capability sidecar 体系（ADR 0146） | `data/processing/algorithm_meta/capability/rs-sar-*.json`（4 个 SAR sidecar）；`scripts/capability_knowledge_tool.cpp` gen-meta/gen-pages |
| 最近 SAR 相关修复（不重做） | PR #938（pair grid/dB preflight）、#934/#854（flatten 2-band/NoData）、#855（各向异性 Horn）、#785、#803、#330；issue 全部 CLOSED |

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | Complex/SLC raster artifact + 通道契约 | `sar_complex.{h,cpp}`（CFloat32 窗口读写、amplitude/phase、通道身份、sentinel 策略、tile 流）+ `test_sar_complex.cpp` |
| B | PolSAR 核心链 | `sar_polsar.{h,cpp}`（T3/C3 ensemble、Pauli、H/A/α Cloude-Pottier、Freeman-Durden、Yamaguchi、特征稳定性）+ `sar_hermitian3` 确定性 3×3 Hermitian 特征分解 + `rs:sar_polsar_decompose` + 已知答案测试 |
| C | InSAR 基础链 | `sar_insar.{h,cpp}`（配对 preflight、干涉图/相干性、Goldstein 相位滤波、解缠参考实现或 provider contract、LOS 形变）+ `rs:sar_interferogram`/`rs:sar_phase_filter`/`rs:sar_displacement`（+`rs:sar_coregister` 视范围）+ 合成 SLC 已知相位测试 |
| D | 多时相 SAR 时间语义 | `sar_temporal_events.{h,cpp}`（acquisition-date 契约、event dating、first/last-change、trajectory、anomaly、缺失景/不规则间隔）+ `rs:sar_temporal_events` + `rs:sar_temporal_stats` additive dateMap |
| E | 集成 + capability/help/docs | 算子注册、sidecar JSON（authored enrichment）、gen-meta/gen-pages、`docs/processing/sar-domain.md` 追加章节、baseline 几何助手（B⊥）并入 `sar_orbit` |
| F | 对抗 review + 修复 + 最终验证 + PR | Phase 7-9 全套 |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M tokens) |
| --- | --- | --- |
| 0 | 基线考古 + 能力矩阵 + planning 落盘 | 18 |
| 1 | 契约设计（complex/PolSAR/InSAR/temporal 语义 + ADR/DECISIONS） | 36 |
| 2 | WP A：complex/SLC artifact 基础 | 48 |
| 3 | WP B：PolSAR 核心链 | 54 |
| 4 | WP C：InSAR 基础链 | 54 |
| 5 | WP D：多时相时间语义 | 30 |
| 6 | WP E：集成（注册/sidecar/knowledge/docs/scale+edge 测试） | 24 |
| 7 | Phase 7 对抗 review（≤2 只读 subagents） | 12 |
| 8 | review 发现修复 | 14 |
| 9 | 最终验证 + rebase + PR | 10 |
| 合计 | | **300** |

## Completion gate

1. `git log --oneline origin/master..HEAD | wc -l` ≥ 5 且含独立 integration commit → 交付非单 patch。
2. `ctest -R "Sar|sar" -j1`（本 track 新增 + 既有 SAR 族）在最终 HEAD 上全 passed。
3. `ls .planning/advanced-sar-polsar-insar-10/*.md` 全部存在且 `git ls-files .planning/advanced-sar-polsar-insar-10/` 非空（被 Git 跟踪）。
4. PolSAR：`rs:sar_polsar_decompose` 至少 pauli + h_alpha + freeman_durden + yamaguchi 四种产物有已知答案测试断言。
5. InSAR：`rs:sar_interferogram`→`rs:sar_phase_filter`→`rs:sar_displacement` 链在合成数据上端到端跑通，相位→位移闭合到闭式解（容差断言）；解缠策略（内建或 provider contract）在 DECISIONS.md 有记录。
6. 多时相：argmax/argmin 事件结果携带 declared acquisition dates 的映射证据（result JSON `dates[]` + raster index band 语义文档）。
7. capability pages 重生成后 `capability_knowledge_tool gen-pages --check` 通过（无 drift）。
8. PR 已创建到 master（`gh pr view` 返回 URL），未 merge。
9. REVIEW_LOG.md 中 P0/P1 全部 disposition=fixed（或 review 零 P0/P1）。

## PR runbook (execute verbatim)

1. Worktree 已建于 `../exp-rs-advanced-sar-polsar-insar-10`；全部工作只发生在 worktree 内。
2. `.gitignore` 白名单三行已追加（本文件落盘与 `.gitignore` 一并作为首次 commit）。
3. 每个 Phase 完成后 commit；把 `git status --porcelain` 完整输出粘进 EVIDENCE.md 对应小节。
4. 每个 Phase commit 后 `git fetch origin && git rebase origin/master`；冲突时加载 `.agents/skills/resolving-merge-conflicts/SKILL.md`，其验证步骤按本 GOAL 的 Tests 行执行。
5. 本地验证按 Build resources 硬约束执行；输出进 EVIDENCE.md。
6. 最后提交前执行存在性断言，结果粘进 EVIDENCE.md。
7. `git push -u origin zcode/advanced-sar-polsar-insar-10`。失败时 stderr 原文记入 EVIDENCE.md；含 hook/BLOCKED 字样原样重试一次；仍失败跳到第 10 步。force push 禁止。
8. `gh pr create --base master --head zcode/advanced-sar-polsar-insar-10 --title "feat(sar): Advanced SAR / PolSAR / InSAR Scientific Platform 10.0" --body-file .planning/advanced-sar-polsar-insar-10/PR_BODY.md`。Do not merge. Do not wait for checks.
9. Reviewer 修改请求：同一 track 续跑，逐条回复、修改、commit、push，重跑第 6 步；仍 do not merge。
10. 任何一步无法完成：EVIDENCE.md 写收尾报告（已完成工作包、失败步骤原文、退出码），报告后停止。

## 存在性断言（runbook 第 6 步）

```bash
for f in .planning/advanced-sar-polsar-insar-10/GOAL.md docs/agents/goal-template.md docs/agents/loop-template.md docs/agents/command-vocabulary.md .agents/AGENTS.md CLAUDE.md; do
  grep -ohE '\.agents/skills/[a-z-]+/SKILL\.md' "$f"
done | tr -d '\r' | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done

grep -ohE '`docs/agents/[a-z-]+\.md`|`review/[A-Z_]+\.(md|csv)`|`\.planning/[a-z0-9-]+/[A-Z_]+\.md`' \
  .planning/advanced-sar-polsar-insar-10/GOAL.md docs/agents/*.md .agents/AGENTS.md CLAUDE.md \
  | tr -d '\r`' | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done
```
