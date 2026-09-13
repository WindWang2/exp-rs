# GOAL — cloud-data-fabric-datacube-10 · Cloud-Native Data Fabric / Data Cube 10.0（查询、虚拟化、分块、缓存与按需执行大规模遥感数据）

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/cloud-data-fabric-datacube-10` (worktree `../exp-rs-cloud-data-fabric-datacube-10`, off `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on,
> trigger, or cite online CI.
> **Write scope:** `src/geospatial/fabric/**`（新模块）· `src/geospatial/remote/**`（窄扩展：prefetch/mirror 挂点）· `src/operators/io/**`（additive 新算子）· `tests/test_io_fabric_*.cpp` · `tests/support/**`（新 fixture）· `docs/io/**` · `CHANGELOG.md`（一条）· `.gitignore`（白名单三行）· `tests/CMakeLists.txt`（测试注册，append-only）· `src/cli/cli_commands.*`（additive `data catalog`/`data cube` 命令）
> **Read-only:** `src/geospatial/stac/**`（8/9 代权威，只复用）· `src/geospatial/catalog/**`（9 代 M7 权威，只复用）· `src/geospatial/multidim/**`（9 代 M5 权威，只复用）· `src/geospatial/hints/**`（9 代 M8 权威，只复用）· `src/geospatial/util/**` `src/geospatial/identity/**`（身份/URI 权威，只复用）· `src/app/**` `src/data/**`（桌面 UI，归属 workbench track）· `src/processing/framework/**` `src/workflow/**`（调度权威，归属 concurrency track）· `src/agent/**`（Agent runtime 归属其 harness track）

## Mission

master @ `7d78059d1a` 已有强大的 geospatial I/O 基础：`ResourceUri` 分类器（`src/geospatial/util/resource_uri.h:33-46`）、有界 HTTP 层（`src/geospatial/remote/http_fetch.h:88-98`）、`/vsirangecache/` VSI 缓存 + 磁盘块层 + 全局 in-flight 字节闸（`src/geospatial/remote/range_cache.h:63-92`）、离线闸（`src/geospatial/remote/offline_gate.h:19-44`）、STAC 客户端（search/pagination/searchAll 有界爬取/客户端过滤，`src/geospatial/stac/stac_client.h:149-227`）、内存目录查询引擎（`src/geospatial/catalog/asset_query.h:139-144`）、lazy 4D/5D cube 描述符（`src/geospatial/multidim/multidim_cube.h:69-109`）、locality hints（`src/geospatial/hints/data_locality.h:45-84`）与统一资产身份（`src/geospatial/identity/asset_identity.h`）。

但这些能力是**离散原语**，不是 data fabric：master 上没有跨资产集合的虚拟镶嵌/时间立方体（`grep -rn "mosaic" src/geospatial --include=*.h` 为空）；没有把 catalog→filter→asset set→grid/time planning→chunk/window→cache→operator 串成有界可执行计划的 query planner（`grep -rln "planner\|ChunkPlan" src/geospatial/` 为空）；`s3://` 只被 ResourceUri 归类为 VsiRemote 拼写（`resource_uri.h:40`），没有 credential 注入 seam、没有 profile 化的对象存储 provider scheme；STAC 客户端没有 platform/sensor/asset-role 级别的统一查询入口，本地 STAC 文件集与远程 STAC API 没有共同 service 面；range cache 明确声明自己"NOT an offline mirror"（`range_cache.h:23-24`），断网时已缓存数据之外的读取只能失败，没有显式 mirror 语义；也没有面向 chunk 计划的 bounded prefetch。

做完之后：一个 agent/UI 调用方可以用一份查询意图（bbox/时间/云量/平台/sensor/资产角色）得到一份**可解释、可估算、可取消**的执行计划；计划在 N 个独立场景资产之上按需读窗口（虚拟镶嵌/时间立方体），每个输出像素可溯源到源资产；大数据读取走 `/vsirangecache/`（RAM+磁盘块+validator 失效），可显式 prefetch、可显式 mirror 供离线重放；`s3://` 经 profile seam 进入同一条路。planner 对 100k 级 catalog 的内存占用是 O(page/selected scenes)，不是 O(catalog)。

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the
  default in Autonomy defaults; record every taken decision in
  `.planning/cloud-data-fabric-datacube-10/DECISIONS.md`.
- **Precedence**: this GOAL overrides `.agents/AGENTS.md` / `CLAUDE.md` on conflict.
  AGENTS.md §1's "surface options / clarify" duty is discharged by writing options and
  the taken default into DECISIONS.md.
- **Agent**: `zcode`. **Token budget: 300,000,000**（allocation below）。When a phase
  exceeds 1.5× its allocation: append a budget line (phase, clock time, commands run,
  files touched) to EVIDENCE.md and continue — never stall silently, never ask.
- **Subagents: at most 2**, both read-only（#1 = Phase 7 架构 + 科学/语义正确性审查；
  #2 = Phase 7 性能/并发/生命周期/测试可信度审查）。
  Main agent owns 100% of implementation and judgment. Subagents spawn no further subagents.
  A subagent that fails or returns nothing usable: the main agent performs that review
  inline, records the failure in EVIDENCE.md, and does not spend an extra slot.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only →
  `.planning/cloud-data-fabric-datacube-10/EVIDENCE.md`. Every capability claim maps to a local command + exit
  code, or is explicitly marked not-executed.
- **Branching**: `master` is read-only. The worktree + branch exist (see header); all
  edits happen only inside the worktree.
- **Build entry**: configure via `CMakePresets.json` presets（development preset
  `build-dev`；本机为 Linux，预设若仅覆盖 Windows 平台则按同 preset 的缓存变量集合
  手工 `cmake -S . -B build-fabric10 -G Ninja -DCMAKE_BUILD_TYPE=Release` 等价复现并把
  configure 命令与 exit code 记入 EVIDENCE.md）。`build.cmd` / `configure_*.cmd` at repo
  root carry stale absolute paths from past tracks — do not follow them (defect D-028).
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`;
  Ninja `-j2`, drop to `-j1` when RSS > 70% or load > 1.5× cores; `-j$(nproc)` is
  forbidden. Measure RSS with `ps`; load via `uptime` (Linux). Log CPU/RSS every 60 s
  during long builds. Host disk has ~56 GB free: exactly ONE new build tree
  (`build-fabric10/`, untracked) inside the worktree; never clone extra build dirs.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` before any
  broad run.
- **Exit**: PR created, not merged; worktree retained until merge, then removed.

## Skills (load proactively)

| Skill | Use it for | Phase |
| --- | --- | --- |
| `.agents/skills/resolving-merge-conflicts/SKILL.md` | rebase 冲突时的收敛流程 | PR runbook 步骤 4 |
| `.agents/skills/code-review/SKILL.md` | Phase 7 主 agent 内联 review 的 checklist 起点 | 7 |

写入本表前逐行执行 `ls .agents/skills/<name>/`；不存在的技能不得出现在本表或正文。
引用格式一律带 `SKILL.md` 全路径。
冲突裁决：autonomy=full 下技能的"向用户提问"步骤由 Autonomy defaults 段的预设答案
替代；`resolving-merge-conflicts` 技能自带的验证步骤按本 GOAL 的 Tests 行执行
（targeted `ctest -R <family> -j1`），不做全量套件。

## Autonomy defaults (do not ask — apply these)

1. **格式/来源**：STAC 1.0/1.1 Item/Collection JSON 为 catalog 真值；网格与坐标契约以
   GDAL/PROJ 返回为准（`geospatial/crs/crs_policy` 既有语义）；cube 轴语义复用
   `multidim_cube.h` 的 verbatim+UTC 契约，不发明 CF 元数据；chunk 计划键为 JSON-stable
   结构；时间比较一律用 `util/time_normalization` 的 UTC instant。
2. **失败项处置**：单资产 open/read 失败在虚拟立方体读取中产生**逐资产 typed 错误记录**
   并按 overlap policy 跳到下一资产（结果 provenance 标记该资产 failed）；计划阶段
   （plan）遇到不可解析资产直接 typed 拒绝——计划必须诚实，执行可以降级。catalog 单条
   记录解析失败跳过并计数（summary.carrySkipped），不让 1 条坏记录杀死 100k 页爬取。
3. **命名/编号**：新模块目录 `src/geospatial/fabric/`；新符号冠以 `Fabric`/`Virtual`/
   `ObjectStore`/`Chunk`/`Prefetch` 前缀语义命名；新测试 `test_io_fabric_<area>.cpp`；
   新算子名 `io:catalog_search` / `io:cube_plan` / `io:cube_window` / `io:cache_prefetch`；
   与既有符号冲突时先 grep 全仓库，重名即换名并在 DECISIONS.md 记一行。
4. **资源与超时**：单条 shell 命令上限 10 分钟（build 目标可到 30 分钟）；超时后降级
   （-j2→-j1、拆更小 target）后重试一次，再超时则把命令原文与退出码记入 EVIDENCE.md
   并换 targeted 路径。任何 benchmark 是 evidence 不是 gate。
5. **对外动作**：允许 `git fetch origin`、`git push -u origin zcode/cloud-data-fabric-datacube-10`、
   `gh pr create`（runbook 步骤 7/8）；禁止 force push、禁止 merge PR、禁止触发/等待 CI；
   网络探测只允许 loopback fixture，测试从不打真实外网。
6. **范围外发现**：记入 `.planning/cloud-data-fabric-datacube-10/EVIDENCE.md` 的
   `OUT_OF_SCOPE` 节；P0 级另在 PR_BODY.md 顶部以 `P0 (out of scope)` 标注；不在本 track
   修复。已登记候选：`review/issues/F-OPS-4.md`（io:reproject srcCrsOverride 死参数，
   P1，io 家族共享面）——若 Phase 6 时 master 无人认领且 diff 干净，作为窄修复纳入并
   在 OWNERSHIP.md 记录理由，否则保持 OUT_OF_SCOPE。
7. **依赖新增**：不引入任何新第三方依赖（无新 SDK；对象存储走 GDAL 既有 /vsis3|/vsiaz|/vsigs/
   家族 + 本仓 profile seam）。需要新工具才能继续时，默认改用仓库既有等价物并在
   DECISIONS.md 记录。

## Current state & evidence (verified)

| Fact | Evidence |
| --- | --- |
| master 基线 SHA | `git rev-parse origin/master` → `7d78059d1a6d316d606656759a506d17bc5e3b55` |
| 远端仅 `origin/master` 活跃（无未合并 track 分支） | `git branch -r` → 仅 `origin/HEAD`、`origin/master`（+ itk-upstream 只读镜像） |
| 范围内最近合并 PR | `gh pr list --state merged` → #958 #957 #956 #955 #954 #953 #952 #951 #950 #949 #948 #947 #946（2026-09-12/13） |
| 无 mosaic/虚拟镶嵌实现 | `grep -rn "mosaic" src/geospatial --include=*.h -l` → 空 |
| 无 query planner / chunk plan 实现 | `grep -rln "planner\|ChunkPlan" src/geospatial/` → 空 |
| range cache 自我声明非离线镜像 | `src/geospatial/remote/range_cache.h:23-24` |
| /vsirangecache/ + 磁盘层 + in-flight 闸已存在 | `src/geospatial/remote/range_cache.h:44,83-89` |
| STAC 客户端有 search/pagination/searchAll/客户端过滤 | `src/geospatial/stac/stac_client.h:160-186` |
| 目录查询引擎为内存引擎（非 store） | `src/geospatial/catalog/asset_query.h:139-144` |
| cube 描述符只面向单一 multidim store | `src/geospatial/multidim/multidim_cube.h:105-109`（describeCube( view, variable )，一个 view 一个文件） |
| locality hints 为纯函数 | `src/geospatial/hints/data_locality.h:83-84` |
| 离线闸为进程级 + GDAL 网络拒绝 | `src/geospatial/remote/offline_gate.h:19-44` |
| GeoError 含 Cancelled/ResourceExhausted/Unsupported 等类型 | `src/geospatial/common.h:26-49` |
| 测试已有 loopback range/STAC server fixture（NoRange/Slow/ETag 等行为） | `tests/support/http_range_server.h:42-61`、`tests/support/http_stac_server.h:29` |
| CLI 已有 `data stac|identity|cache status\|clear` | `src/cli/cli_commands.cpp:1802-1803` |
| GDAL 3.13.3（系统） | `pkg-config --modversion gdal` → `3.13.3` |
| 9 代交付清单（避免重做） | `.planning/geospatial-data-fabric-9/FINAL_REPORT.md`（M0–M9 全部落地） |

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | 契约与架构（Phase 1） | `fabric/` 模块全部头文件契约：ObjectStoreProfile、CatalogService、VirtualCube、ChunkPlan、FabricPlan、Prefetch/Mirror；DECISIONS.md ADR 级条目；JSON schema 稳定键 |
| B | 对象存储 seam（Phase 2） | `s3://`（及 az/gs 拼写）→ VSI 映射、credential 注入（open 时注入、不落盘、不入 identity/display）、validator/etag 复用、offline 拒绝、typed unsupported |
| C | 统一 catalog service（Phase 2） | 本地 STAC 文件集 + 远程 STAC API + 内存 AssetRecord 三后端统一接口；platform/sensor/assetRole/cloudCover 统一过滤；有界分页；cancel token；offline typed refusal；credential-safe 诊断 |
| D | 虚拟镶嵌/时间立方体（Phase 3） | 资产集合 → 网格协商（显式或确定性派生）、确定性 overlap policy、quality/mask-aware 选择、on-demand 窗口读取、逐窗口源资产 provenance、无 eager 全镶嵌 |
| E | cube chunk 计划（Phase 3） | 命名维（time/y/x/band）、切片/window/时间范围/band role → 有界 chunk 请求集 + 总数（支持百万级逻辑 chunk 不物化全列表） |
| F | query planner（Phase 4） | intent → stages（catalog_query→asset_selection→grid/time planning→chunk_requests→cache warm→execute）；cost hints（scenes/chunks/bytes/memory/remote calls）；JSON 可检视；执行有预算有取消；O(page/scenes) 内存 |
| G | cache 10.0（Phase 4） | bounded prefetch（chunk 计划→预热，字节预算+取消）；显式 offline mirror（计划→mirror 目录物化，atomic publish；读取优先 mirror 命中）；损坏恢复证据 |
| H | 集成面（Phase 4） | `io:catalog_search` / `io:cube_plan` / `io:cube_window` / `io:cache_prefetch` 算子（operator registry 注册）；CLI `data catalog search`、`data cube plan\|window`、`data cache prefetch`；docs/io/fabric-10.md + CHANGELOG |
| I | 规模与边缘（Phase 5/6） | 100k 合成 catalog 计划内存证据；百万 chunk 计划；取消；慢服务器/range-ignoring 服务器；ETag 变化；mirror 离线重放；空/单资产/all-NoData/损坏元数据 |
| J | review/verify/PR（Phase 7/8/9） | 2 只读 subagent 对抗审查 + P0/P1 清零 + 最终 HEAD 全量本地验证 + PR |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M tokens) |
| --- | --- | ---: |
| 0 | 基线审计 + 去重 + GOAL 落盘（已完成大半） | 18 |
| 1 | WP-A 契约与架构（headers + ADR） | 48 |
| 2 | WP-B/C foundation（object store + catalog service + 单测） | 54 |
| 3 | WP-D/E 虚拟立方体 + chunk 计划 + 单测 | 46 |
| 4 | WP-F/G/H planner + cache 10 + 集成面 | 38 |
| 5 | WP-I 规模/性能证据 + 文档 | 34 |
| 6 | 边缘/失败矩阵 + WP-I 收尾 | 22 |
| 7 | 交叉复核（≤2 只读子代理）+ 修复 | 26 |
| 8 | Rebase + docs 同步 + 最终验证 + PR | 14 |

总和 = 300。计量方式：会话内无 token 计数接口；以可测量代理指标计——每 Phase 结束时把
`时间戳 / 工具调用次数 / 触及文件数` 记入 EVIDENCE.md 预算节，对照阶段包线判断超支。
阶段超 1.5× 包线：按 envelope 预算行上报后继续。总预算耗尽而 Completion gate 未达成：
停止开新工作，按已完成工作包走 PR runbook 出 PR，PR_BODY.md 顶部声明未完成项清单。

## Required planning files

`.planning/cloud-data-fabric-datacube-10/`（worktree 内）：`GOAL.md`（本文逐字存档）·
`PLAN.md` · `BASELINE.md` · `DECISIONS.md` · `EVIDENCE.md` · `REVIEW_LOG.md` ·
`PR_BODY.md` · `OWNERSHIP.md` · `CAPABILITY_MATRIX.md` · `ARCHITECTURE.md` ·
`MILESTONES.md` · `PROGRESS.md` · `PERFORMANCE.md`。

## Completion gate

1. worktree 从 `origin/master` @ `7d78059d1a` 建立，`.gitignore` 白名单生效 →
   `git check-ignore -v .planning/cloud-data-fabric-datacube-10/GOAL.md` → exit 1（无输出）。
2. `src/geospatial/fabric/` 模块存在且被 `src/geospatial/CMakeLists.txt` 编译 →
   `grep -c "fabric/" src/geospatial/CMakeLists.txt` ≥ 6（新源文件注册）。
3. 对象存储 seam：s3 拼写映射 + credential 注入 + 离线拒绝有测试 →
   `ctest -R test_io_fabric_object_store -j1` → 0 fail。
4. 统一 catalog service 三后端 + 过滤 + 分页 + 离线 →
   `ctest -R test_io_fabric_catalog -j1` → 0 fail。
5. 虚拟立方体窗口读取 + overlap policy + provenance →
   `ctest -R test_io_fabric_cube -j1` → 0 fail。
6. chunk 计划 + planner（可检视/可估算/可取消）→
   `ctest -R "test_io_fabric_plan" -j1` → 0 fail。
7. prefetch/mirror → `ctest -R test_io_fabric_cache -j1` → 0 fail。
8. 规模证据（100k catalog O(page) 内存、百万 chunk）→
   `ctest -R test_io_fabric_scale -j1` → 0 fail；内存断言在测试内。
9. 新算子经 registry 可执行 → `ctest -R test_io_fabric_operators -j1` → 0 fail。
10. 既有 io 家族无回归 → `ctest -R "test_io_(uri|stac|range_cache|remote_range|catalog_query|multidim|hints)" -j1` → 0 fail。
11. review P0/P1 清零且 disposition 落档 → `REVIEW_LOG.md` 每条 finding 有 disposition + evidence。
12. 存在性断言两条命令（goal-template「存在性断言」节原样执行）输出为空。
13. PR 已创建未合并 → `gh pr view <url> --json state,headRefName` → `OPEN` + 本 track 分支。

## PR runbook (execute verbatim)

1. `git worktree add ../exp-rs-cloud-data-fabric-datacube-10 -b zcode/cloud-data-fabric-datacube-10 origin/master`；此后全部工作（含本 GOAL.md 的落盘）只发生在 worktree 内。✅（已执行）
2. 在 worktree 内向 `.gitignore` 追加本 track 白名单（三行模式，照
   `.planning/prompt-command-hygiene-review/` 条目），执行
   `git check-ignore -v .planning/cloud-data-fabric-datacube-10/GOAL.md`，确认无输出
   （exit 1）后，把 `.gitignore` 与 `.planning/cloud-data-fabric-datacube-10/GOAL.md`
   一并作为首次 commit。
3. 每个 Phase 完成后 commit 一次；把 `git status --porcelain` 的完整输出粘进
   EVIDENCE.md 对应 Phase 小节。
4. 每个 Phase commit 后执行 `git fetch origin && git rebase origin/master`；冲突时
   加载 `.agents/skills/resolving-merge-conflicts/SKILL.md`，其验证步骤按本 GOAL 的
   Tests 行执行（targeted `ctest -R <family> -j1`）。
5. 本地验证按 Build resources 硬约束执行；输出进 EVIDENCE.md。
6. 最后提交前执行存在性断言（goal-template「存在性断言」节两条命令），结果粘进
   EVIDENCE.md。
7. `git push -u origin zcode/cloud-data-fabric-datacube-10`。失败时：stderr 原文记入
   EVIDENCE.md；若错误含 hook/BLOCKED 字样，原样重试一次；仍失败则跳到第 10 步写收尾
   报告并停止（PR 未建成即 track 的如实终态）。`git-guardrails-claude-code` 技能仅描述
   hook 机制——禁止在本 track 内执行它的任何安装步骤。force push 在任何情况下禁止。
8. `gh pr create --base master --head zcode/cloud-data-fabric-datacube-10 --title "<type>(<scope>): <summary>" --body-file .planning/cloud-data-fabric-datacube-10/PR_BODY.md`。Do not merge. Do not wait for checks. Report the PR URL and stop.
9. 收到 reviewer 修改请求：视为同一 track 的续跑——逐条回复、修改、commit、push，
   重跑第 6 步断言；仍 do not merge。预算从原 track 余量扣除；超 1.5× 按预算行上报。
10. 任何一步无法完成（push 被拦且重试无效、rebase 无法收敛、gh 不可用）：在
    EVIDENCE.md 写收尾报告（已完成工作包、失败步骤原文、退出码），向用户报告后停止。
