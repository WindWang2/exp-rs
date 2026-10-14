# REVIEW_R2 — PR #1498 第二轮审查

> **独立性声明（必读）**：本轮由**实现者本人**执行，不是未参与实现的第三方 reviewer。
> R12-G 指令要求"使用未参与实现的 reviewer 进行至少两轮独立 review"，**该条件本轮未满足**：
> 第一轮 R1 由 general-purpose-4（独立）完成；R2 因子代理配额 429 未执行，后由主 Agent 自行补做。
> 因此本报告的定位是**实现者自查 + 独立复现验证**，不能替代真正的 R2。
> 合并前仍建议由一名未参与实现的 reviewer 复核。

- PR: https://github.com/WindWang2/exp-rs/pull/1498
- 分支 `hardening/r12-g-build-test-offline` → `master`，4 commits，9 files，+420/−30
- 基线 `origin/master = 28abef35`；审查时 `origin/master` 已前进 4 commits
- 审查方式：不采信 PR_BODY 的任何声明，全部用**独立构造的夹具/命令**复现

---

## 1. 已独立证实为正确的声明（15 项）

| # | PR 声明 | 我的独立复现 | 结论 |
|---|---|---|---|
| V1 | G09-1 junction 逃逸：修复前 `BUNDLE VERIFY PASS exit 0` | 构造**精确攻击夹具**：真 junction `jlink → outsidedir`，manifest 以 `jlink/secret.txt` 登记且 size/sha256 均取自**包外真实文件**。HEAD 版 → `BUNDLE VERIFY PASS exit=0`；PR 版 → `FAIL exit=1` + 两条 typed 诊断（`path escapes bundle through a link` / `reparse point directory escapes bundle`） | ✅ **安全缺陷真实存在且已修复** |
| V2 | G09-2 pwsh7 `Int64` 使每个正确包判 FAIL | 同一正确包：修复前 5.1=PASS / pwsh7=FAIL；修复后两者均 PASS | ✅ 已修；⚠️ 见 F8 |
| V3 | `--jobs 8` 被拒 exit 2，抬 cap 路径仍放行 | `--jobs 8` → exit 2 + 明确 stderr；`SICNU_BUILD_JOBS_CAP=99 --jobs 8` → 通过 resolve_jobs 并进入守卫 | ✅ |
| V4 | `SICNU_BUILD_REQUIRE_GUARD=1` 真实拒绝（exit 1，payload 未跑） | `r1fix_repro.sh` 复现：exit 1，`PAYLOAD-RAN` 未出现 | ✅ R1-01 已闭合 |
| V5 | G05-1 netcdf 真实 configure | `r12g-configure7.log` exit=0，`NETCDF_INCLUDE_DIR` 入 CMakeCache（改动前缓存中只有 `NETCDF_LIBRARY`） | ✅ |
| V6 | G14 旧版 `13/13 green` 是硬编码谎言 | `_green_summary` 用 4 种真实 ctest 日志形状测试：全过 → `13/13`；1 败 → `12/13`；空匹配/无汇总行 → `count unproven`（诚实降级）。HEAD 版在同一空注册构建目录打印 `PASS O1 run 1: 13/13 green` | ✅ |
| V7 | G13-2 OTB 复制 0 文件仍打印成功 | harness：空源 → `exit 1` + `ERROR: OTB is required but 0 files were copied`；有内容源 → `exit 0` + `OTB bundled successfully (4 files)` | ✅ |
| V8 | conformance 18 checks / 1 failed → 0 failed | 亲跑：`conformance: 18 checks, 0 failed`，exit=0 | ✅ |
| V9 | `selftest: ALL PASS` | 亲跑通过（R1-07 marker 修复后） | ✅ |
| V10 | netcdf 改动不会打红 CI | Tier1/Tier2（Linux，`ENABLE_TESTS=ON`）装 `libgdal-dev`，而 **`libgdal-dev` 在 Ubuntu 24.04 的依赖列表明确包含 `libnetcdf-dev`**（launchpad/packages.ubuntu.com 实证）→ 库与头都在；Tier3 Windows 与 macOS 均为 `ENABLE_TESTS=OFF` → 该 CMake 块不可达 | ✅ **无 CI 风险** |
| V11 | `--no-tests=error` 不会对未构建目标误报 | 读 item 循环：先 `find_binary`，二进制缺失 → `status="not-built"` 且**根本不调 ctest**；只有"二进制在、注册丢了"才触发 | ✅ 无误报风险 |
| V12 | `executed==0` 判失败不会误伤 `--resume` | resumed-pass 项计入 `passed` → executed>0 → 全 resume 运行不会误判失败 | ✅ |
| V13 | `note`/`fail` 文本不会被命令替换捕获进 argv | `note() { echo ... >&2; }`、`fail() { ... >&2; }` 均走 stderr | ✅ 我原本怀疑的缺陷不存在 |
| V14 | rebase 干净 | `git merge-tree --write-tree origin/master HEAD` → 无冲突；master 对 `tests/CMakeLists.txt` 的 48 行改动在 1615/4131/11071，本 PR 在 424/579/617，**零重叠** | ✅ |
| V15 | `resolve_jobs` 的 `exit 2` 能传出命令替换 | `scripts/build.sh:28` 为 `set -euo pipefail` | ✅ |

---

## 2. 发现的问题

| ID | 严重度 | 问题 | 证据 / 位置 | 建议 |
|---|---|---|---|---|
| **F1** | 🟡 P3 → **我的误判，已更正** | **原文声称有误，留痕更正**：我原判"`docs/development/README.md:19` 示例 `-- cmake --build build-dev --parallel 8` 被新守卫拒绝、从未可用"。实测**不成立**：该 `--parallel 8` 位于 `--` 之后，属 **cmake payload** 而非守卫，守卫将其**钳制**为 2 后放行（桩 payload 收到 `--parallel 2`，exit 0；守卫打印 `clamped parallelism: --parallel 8 -> 2`）。真正没写清的只有一件事：守卫**自身**的 `--parallel` 超过 `--max-parallel` 是**拒绝**（exit 2），与 payload 的钳制是两套语义，而两份文档都没区分 | 桩 payload 打印 argv + 三种参数组合实测 | ✅ 已在 `docs/development/README.md` 步骤 4 区分两种语义（b4ecc96fa） |
| **F2** | 🟡 P3 | **新增开关零文档**：`SICNU_BUILD_GUARD_WAIT` / `SICNU_BUILD_REQUIRE_GUARD` / `SICNU_BUILD_NO_GUARD` 三个面向用户的旋钮在**任何被跟踪的 .md 中都没有说明**，只存在于被 gitignore 的 `.planning/` 文档 | 全仓库 `grep --include=*.md` | ✅ 已补：`docs/development/README.md` 新增规则 5（四个开关 + 二者同设即矛盾退 1），`scripts/dev/README.md` 表格行区分钳制/拒绝并点名 `--max-parallel` 提额路径；三条声明均实测相符（b4ecc96fa） |
| **F3** | 🟠 P2（流程） | **证据链不可见**：`.gitignore:142` 忽略 `.planning/*`，因此 BASELINE / ORACLE_LEDGER / 三份审计 / FINAL_REPORT / 验收矩阵**全部不在 PR 里**，其他 reviewer 只能看到 PR_BODY 的摘要转述，无法独立核验 | `.gitignore:142`；`git ls-files .planning` 仅含既有的 RS14 目录 | ✅ 已按 master 上的 **R12-E 先例**解决：`git add -f` 强制纳入（R12-E 同样未改 `.gitignore`，见 `9c3272ed8`）。本方向 13 份 markdown 证据 + 6 个可跑复现器已随 PR 提交；派生的 `verify_HEAD.py`（`git show HEAD:` 的副本）不提交，避免腐烂副本 |
| **F4** | 🟡 P3 | **租约覆盖不完整**：租约只包住 `scripts/build.sh` 自己的 payload。`verification_ladder.py`、`perf_observatory_verify.py`、`build-examples.sh` 以及任何直接 `cmake`/`ctest` 调用都绕过它，"一个构建目录一个写者"的不变式对这些路径并未成立 | 调用点清点 | 记为进一步项；至少在被绕过的脚本里加提示 |
| **F5** | 🟡 P3 | **PR 落后 master**（审查时为 4 commits，处置时已 28） | `git rev-list --left-right --count origin/master...HEAD` | ✅ 已用**合并**而非 rebase 处置：仓库自身规则（`docs/development/README.md` 规则 3）明令禁止 force-push，而 rebase 必须强推。合并前核验两侧唯一重叠文件 `tests/CMakeLists.txt` 的 hunk 零重叠（master 1615/4131/11071/15315 vs 本分支 424/579/617），合并干净无冲突；合并后一致性回归复跑 **18 checks, 0 failed** |
| **F6** | ℹ️ | `test_resource_guard.py` 1/16 flaky 是**既有**问题（HEAD 版同条件同样失败），且**CI 爆破半径为零**（未注册进 ctest，无 workflow 引用） | HEAD 对照实测 + grep | 单独开 issue，不阻塞本 PR |
| **F7** | ℹ️ | `--no-tests=error` 的收紧会把**潜在的注册漂移**翻成红灯。这是设计意图，但若有人把 ladder 当合并门禁，预期会出现新红车道 | 设计性阅读 | 在 TEST_PLAN/DECISIONS 中已声明即可 |
| **F8** | 🟡 P3 | **PS 侧改动无自动化覆盖**：`conformance.py` 的 PS lane 在本机 `not-executed: PS verify lane (pwsh unavailable on this host)`。G09-2 与 `Get-RelPath` 归一化两个修复只经过人工验证，没有任何自动化测试守护，未来回归无人发现 | conformance 输出 | 建议后续给 PS 孪生补一条 hermetic 用例 |
| **F9** | ℹ️  Nit | `_unsafe_bundle_rel` 中 `normalized.startswith("/") or rel.startswith("\\")` 的第二个子句是**死代码**：`rel` 的反斜杠已被替换成 `/`，前者必然先命中 | `scripts/verify_bundle_manifest.py` | 可删，留着无害 |

---

## 3. 必须知道的未闭环项（PR 正文已如实记录，此处复核确认）

- ❌ **L4 ctest 全 `0xc0000135`**：Qt bin / vcpkg debug bin / VC DebugCRT 三种 PATH 补全均无效。L3 编译是好的（`test_paths.exe` 2,393,600 B 真实产出）。属环境级阻塞，与本 PR 改动无关。
- ❌ **G13-1** AppImage 工具链校验和仍 `PENDING`，修复需联网，本机离线。
- ❌ **G10 全部 P2** 均为 `.cmd/.bat`，本机 cmd.exe 被安全策略阻断，改了无法验证 → 未动。
- ❌ **G02/G03/G11** 属其他方向所有权，只审计不修。
- ❌ **真正的独立 R2 未执行**（见开头声明）。

---

## 4. 结论

**代码质量：✅ 可以合并。**

F1/F2/F3/F5 已在合并前全部处置完毕（见上表各行），F4/F6/F7 记为后续项、不阻塞。

| 维度 | 评价 |
|---|---|
| 正确性 | 15 项声明全部独立复现通过；** headline 安全缺陷（junction 逃逸）用 A/B 对照实证** |
| 反作弊 | 未删测试、未放宽超时、未改 golden；所有新增判据方向都是**收紧**；唯一"变绿"是真实缺陷修复所致 |
| CI 安全性 | 已实证无风险（netcdf 由 libgdal-dev 传递提供；Win/mac 不编译测试） |
| 误报风险 | 已实证无误报（未构建目标不调 ctest；resume 不误判） |
| 过程合规 | ⚠️ 独立 R2 由实现者本人补做，独立性仍未满足（用户已授权合并，留痕备查） |
| 文档 | ✅ F1 误判已更正、F2 已补文档、F3 证据已随 PR 提交 |
| 版本基线 | ✅ 已合并 origin/master，唯一重叠文件零 hunk 冲突，合并后回归 18/18 |

**遗留（不阻塞，建议后续）**：F4 租约未覆盖 ladder / perf verifier / 直接 cmake·ctest 调用；F6 `test_resource_guard.py` 既有 flaky 建议单开 issue；F7 ladder 收紧后可能出现新的注册漂移红灯，属设计意图。

**另记一笔自我更正**：F1 是我这轮审查自己的误判——把 cmake payload 的 `--parallel 8` 当成守卫自身的参数。留痕而非抹掉，因为它恰好证明"不采信任何一方的声明、全部自己跑一遍"这条纪律在本轮确实抓到了东西（虽然是抓自己）。
