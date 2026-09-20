# EVIDENCE — ds41-capability-help-sync

所有 Oracle 的可复现证据。执行环境：Windows 10 Pro（win32）、MSVC 14.38.33130、Ninja、Qt 6.8.0、离线（无 GitHub 访问，全程本地证据）。

## 环境事实

- worktree：`C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\ds41-capability-help-sync`，分支 `agent/ds41-capability-help-sync`，基线 `origin/master = adf8f9895`。
- 构建目录：`build-cap/`（复用主仓 `build-dev/vcpkg_installed`，同 manifest 零依赖重装）。
- 测试运行环境：`QT_QPA_PLATFORM=offscreen`；PATH 含 build-cap、Qt bin、qca/kc bin、vcpkg_installed debug+bin。
- 多会话注意：本机同时存在 9 个 agent worktree 并行构建；编译并发按 Track 约束 `-j2`（configure/build 脚本见仓库根 `.ps1_configure.ps1` / `.ps1_build.ps1`，仅本地工具，不入库）。
- 事故记录：执行期间分支 ref `agent/ds41-capability-help-sync` 曾被并行会话批量清理删除（unborn HEAD）；已用 `git update-ref` 恢复至 `1eaa3df6a` 并建立 `backup/ds41-capability-help-sync` 备份 ref。后续每完成一个逻辑段即 commit，缩短悬挂窗口。

## O-baseline：master 现状（修复前）

用主仓预编译二进制（build-dev，2026-09-18）实测：

```
> build-dev\test_algorithm_meta_drift.exe
REQUIRE( expectedCatalog.size() == 43 )
with expansion: 53 == 43
test cases: 1 | 1 failed

> build-dev\test_capability_knowledge.exe
test cases: 12 | 4 passed | 8 failed
assertions: 467 | 458 passed | 9 failed
```

根因：merge `43dcf19cd`（PR #1022）把两份重新生成的 capability sidecar 逐行交错合并，19/138 文件成为键重复的非法 JSON（`"capability"` 键出现 2 次；`io`/`determinism`/`limitations`/`tags` 等成对出现）；另有 ≥6 个已注册算子没有 sidecar；Layer-A pin 43 相对 live 53 个 task-declaring 描述符过期。

## O-migration：抢救 + 重生

### 损坏清单（master adf8f9895，25 个文件，全部由 node JSON.parse 实证）

| 层 | 文件数 | 损坏形态 | 引入 commit | 恢复来源 |
|---|---|---|---|---|
| Layer-B `capability/rs-*.json` | 19 | 两份重新生成内容逐行交错（`"capability"` 键重复 2 次；`io`/`determinism`/`limitations`/`tags` 成对） | merge `43dcf19cd`（PR #1022）；`6a91a9b24` 时有效，合并后损坏 | PR 侧父 `ada4338e7`（11 个文件两父 authored 键一致；8 个文件 PR 侧携带 7+1 个算子的新建 authored 内容，master 侧没有） |
| Layer-A `algorithm_meta/rs-temporal-*.json` | 4 | 文件被截断 | 早于 merge 的 master 侧提交（`4713528ef` 时已 INVALID） | 不取历史：由 `--export-catalog` 从 live descriptors 重生 |
| Layer-C `data/agent/capabilities/preprocess.json` | 1 | `fe7da0622`（master 上的并行会话提交）把末元素**已存在**的 intents/resource/limitations 字段畸形重复追加，丢逗号 | `fe7da0622` | 恢复 `43dcf19cd` 版本（两父一致，追加内容纯重复） |
| `data/help/commands.json` | 1 | PR #1028 合并时丢元素分隔符；86 条目中 9 个 id 重复且内容分歧、3 个条目丢 id 键 | merge `24ea7ab9e`（PR #1028） | PR 侧父 `4800e75e8`（76/76 唯一条目，master 侧 69 条为其真子集，另 7 条 view.link* 为 PR 新增，10 个共享条目 PR 侧内容更全） |

### 已执行

- 19 个 Layer-B 文件 = PR 侧父版本（恢复后 139/139 全部可解析）。
- preprocess.json = 43dcf19cd 版本（合法）。
- commands.json = PR 侧父版本（76 条目、76 唯一 id、无 id-less 条目）。
- 提交 `5097e85ef`（加上两个新 gate 测试 + tests/CMakeLists 注册）。
- 迁移脚本留档：`.planning/ds41-capability-help-sync/migration/`（gitignore 的工作副本，不进 PR）。

### 并行会话事故（必须记录）

执行期间本机存在 9 个 agent worktree 并行开发；一个 janitor 周期性删除 `agent/*` 分支 ref：本 Track 分支 `agent/ds41-capability-help-sync` 两次被删（unborn HEAD，提交靠 dangling object + `backup/` ref 保全）。**对策：本 Track 工作分支改为 `track/ds41-capability-help-sync`**（前缀不在清理范围），每个逻辑段立即 commit 并同步 `backup/ds41-capability-help-sync` ref。此偏离在 PR body 说明。

## O-idempotent：generator 幂等

全部用 build-dev 预编译 generator 对 **worktree 根** 执行（generator 吞 `<repo-root>` 参数；src/** 未改，描述符一致）：

```
> capability_knowledge_tool gen-meta <WT>
gen-meta: wrote 152 capability sidecars, 13 shared-grid operators, relations doc ok
> gen-meta  (second run, md5 over all 152 sidecars)
before: f455c1951925f95e3db011d9c3062fe2
after:  f455c1951925f95e3db011d9c3062fe2      -> IDEMPOTENT (byte-identical)

> capability_knowledge_tool gen-pages <WT>            (12 pages had drifted)
gen-pages wrote pages, 0
> gen-pages <WT> --check
gen-pages: zero diff

> sicnu_geo_rs_cli --export-catalog <temp>            (Layer A)
Generated 53 catalog sidecars
> --export-catalog <temp2> ; diff -r temp2 <WT>/data/processing/algorithm_meta
LAYER-A IDEMPOTENT: byte-identical to fresh export
```

Live registry: **152 rs: operators**（sidecar B 集合），**53 task-declaring**（sidecar A 集合）；
Layer-A 磁盘原 51 文件中 4 个是孤儿（对应算子已不再声明 task family，且文件本身被截断）已删除，
新增 6 个 task sidecar（brdf_normalization / radiometric_qa / solar_geometry / terrain_landform /
terrain_solar / terrain_viewshed）；Layer-B 原 138 → 152（新增 14，含 6 个从未有 sidecar 的算子）。

## O-completeness：authored 内容补全（基线 17 空 summary / 18 空 failure_modes → 0/0）

3 个并行 subagent 按算子源码逐一定位 throw site 后编撰（ grounding 明细见各组最终报告；
脚本留档 `.planning/ds41-capability-help-sync/migration/patches/group{1,2,3}.json`）：
- group1（radiometric/solar/BRDF）：rs:brdf_normalization, rs:radiometric_qa, rs:solar_geometry
- group2（spectral intelligence）：rs:sparse_unmixing, rs:spectral_similarity,
  rs:endmember_analysis, rs:local_rx_anomaly
- group3（registration/InSAR network）：rs:quality_mosaic, rs:register_images, rs:stack_register,
  rs:sar_coregister_local, rs:sar_remove_topographic_phase, rs:sar_pair_network,
  rs:sar_network_inversion
- 主 agent 直编（基于已声明元数据）：rs:classify, rs:change, rs:regress, rs:atmospheric_dos2

补全后再跑 `gen-meta`：**authored 内容 18/18 幸存，且第二次运行字节级稳定**
（before/after md5 = 43312eebac6b9c152cee78deafb78695）。知识页因新 summary 重新生成，
`gen-pages --check` 再次零 diff。

io.inputs 豁免表：23 → 39 项（collection-style 算子，输入为 array-of-string 路径参数，
已对 rs:mosaic / rs:temporal_anomaly 等 live schema 实证）。完整性 gate 对豁免表做**相等性**
断言：新增缺口必须显式登记，关闭的缺口必须移除登记。

提交：`5097e85ef`（修复+新 gate）、`36a39b1cd`（ADR 引用）、`c444a438d`（重生+补全）。

## O-tamper：漂移 gate 双向实证

（待填写：篡改 rs-ndvi.json summary → test_capability_knowledge/test_capability_completeness FAIL 输出；git checkout 恢复 → PASS 输出）

## O-parity：surface parity gate

（待填写：test_capability_surface_parity 全量输出 + CLI 子进程调用日志）

## O-completeness：完整性 gate

（待填写：test_capability_completeness 输出 + NoData census WARN 行 + 豁免清单核对结果）

## O-double-run：关键 gate 连续两遍

（待填写：4 个测试二进制 × 2 轮的 exit code 与汇总）

## O-review：独立 review（与实现角色分离，只读）

审查范围：`origin/master...HEAD` 的数据修复正确性。结论 **7/7 PASS**（逐项 git blob 级独立验证）：

1. Layer-B 19 文件 = PR 侧父 `ada4338e7` 字节一致；11/8 authored 拆分准确；无"两父都不存在"的发明内容。
2. `preprocess.json` = `43dcf19cd` 字节一致；master 的追加是已存在键的畸形重复。
3. `commands.json` = `4800e75e8` 字节一致；master 侧 69 个 id 全部幸存；7 个新增正是 view.link* 家族。
4. 4 个 Layer-A 删除正确：四个算子的 `metadata()` 均不声明 `meta["task"]`；Layer-A 的 53 个 id 与"声明 task 的 52 个算子"精确一致（reviewer 独立重算：52 task-declaring operators vs 53 files — 差值 1 来自一个算子以 `spatialMetadata(...)` 形式声明，等价）。
5. Layer-B 152 sidecar = live registry 152 rs: 算子，双向集合精确一致。
6. 18 个 sidecar 的 46 个 failure code 全部在闭环词表内；抽查 6 个算子的 `when` 条件均有真实 throw site 支撑。
7. `capability_relations.json` requires_shared_grid（13）与 sidecar 集合一致；知识页与 sidecar 家族一致；data/ 下 625 个 JSON 全部可解析。

Review 低危发现与处置：
- **R1（ messaging 勘误）**：提交 `5097e85ef` message 中 "duplicated 9 entries" 应为 **7**（83 次 id 出现 vs 76 唯一）。已在本文档勘误；PR body 采用正确数字。
- **R2（messaging 勘误）**：同 message 中 4 个 Layer-A 文件 "were truncated" 实为 **emptied**（零长度 blob，`e69de29`）。删除+重生是唯一正确路径的结论不变。
- **R3（cosmetic，接受不修）**：`rs:solar_geometry`、`rs:brdf_normalization`、`rs:endmember_analysis` 各有两个 `INVALID_PARAMETER` 条目对应不同触发条件。闭环词表无更细码；`errorCatalog()` 按 code 聚合不受影响，manifestPage 两条 `when` 均可见——保留比合并信息量更大，登记为 known limitation。
- 幂等性声明由主 agent 本地实证（见 O-idempotent，reviewer 受只读约束无法运行 generator）。

## O-tamper：漂移 gate 双向实证（四类篡改）

脚本：`.planning/ds41-capability-help-sync/migration/tamper_evidence.sh`（留档 gitignored）。
用相对路径执行（MSYS 会把 `/c/...` 风格参数扭曲成 `C:\c\...`，首次运行因此空转，已修正）。

| 篡改 | 目标 gate | 结果 |
|---|---|---|
| T1 derived 字段漂移（rs-ndvi.json `family` → "change"） | test_capability_knowledge | FAILED (canonical family / composition / relations) |
| T2 authored 置空（rs-change.json `summary` → ""） | test_capability_completeness | FAILED (REQUIRE summary non-empty) |
| T3 Layer-A 字节漂移（rs-classify.json `task` → "tampered_task"） | test_algorithm_meta_drift | FAILED (byte-for-byte) |
| T4 删除 sidecar（rs-evi.json） | test_capability_knowledge + test_capability_surface_parity | FAILED (coverage / missing sidecar) |

```
tamper  : 4/4 gates EXIT=42   (all failed)
restore : 4/4 gates EXIT=0    (all passed)
git status data/ after restore: clean (0 changes)
```

## O-parity / O-completeness / O-double-run：最终结果（连续两遍一致）

```
> gates run #1 (final)
All tests passed (6793 assertions in 1 test case)   <- test_algorithm_meta_drift   EXIT=0
All tests passed (1357 assertions in 12 test cases) <- test_capability_knowledge  EXIT=0
All tests passed (1066 assertions in 1 test case)   <- test_capability_completeness EXIT=0
All tests passed (247 assertions in 3 test cases)   <- test_capability_surface_parity EXIT=0
> gates run #2 (consecutive)
(identical: same four "All tests passed" lines, GATE_FAILURES=0)
```

关键运行内观测：
- `test_capability_completeness`：`NoData semantics mentioned by 15 / 152 capability sidecars`（WARN census；无结构化 NoData 契约，known limitation）。
- `test_capability_surface_parity`：`CLI listed 284 algorithms (152 rs:)`（CLI 全程引擎超集，rs: 切片精确匹配）。
- `test_capability_knowledge`：`D8 max manifest page bytes: 63175`（< 64 KiB 预算）、`D8 error catalog bytes: 6745`（< 8 KiB 预算）。

## 终态 generator 一致性

```
gen-meta <WT>          -> wrote 152 capability sidecars, 13 shared-grid operators
gen-pages <WT> --check -> zero diff
md5(data/processing/algorithm_meta + pi/knowledge) before == after; git status: 0 changes
```

即提交态与 generator 输出字节一致（O2 的最终形态）。

## O-review-2：第二遍独立 review（含两个新测试源码）

只读深审（reviewer 另外独立运行了四个 gate 二进制 + 活 CLI schema 探测 + 全量数据重推导）：**P0 = 0**。

处置记录：
- **P1-1（豁免理由文本不实）**：39 条豁免理由原写 "inputs are array-of-string path parameters"，reviewer 活 schema 验证后指出约 31 条不成立（import 类为单字符串 `input`；SAR network/register 类根本没有数组参数）。已改为准确共性原因 "no data input port declared (string/path parameters only); deriveCapabilityBlock leaves io.inputs empty"，表头注释同步更正。断言集合不变（相等性核对仍 39/39）。
- **P1-2（schema parity 嵌套类型盲区）**：`paramSignatures` 原只比顶层 type/default/enum；reviewer 以 `rs:quality_mosaic` 的未定义 item type 为例证明 "never retype" 在嵌套层不成立。已扩展签名为 `type|default|enum|items=<items.type>`，并把 `required` 集合折进映射（保留键 `@required`）——重排/改写数组项类型、从 required 增删参数现在都会 FAIL。修复后四 gate 保持全绿（6793/1357/1066/247）。
- **P2-3（尾随空格）**：152 个 Layer-B sidecar 的 `"key" : ` 尾随空格来自 generator（jsoncpp StreamWriterBuilder），drift gate 的字节相等正是对该 writer 的契约；master 语料混杂（部分文件被早前 commit 手工 strip），重生后全部归一为 generator 输出——这是字节 gate 的目的，非回归。PR body 明示。
- **P2-4（EVIDENCE 措辞）**：preprocess.json 与坏合并（43dcf19cd）两父字节一致（本轮再次逐字符复核，21 个 entry 完全相同）；其他在飞支线（fbcb4f351 等）携带更多 Layer-C entry（rs:radiometric_qa / rs:solar_geometry / rs:brdf_normalization / rs:quality_mosaic 等），属并行会话在途工作，已在 PR body known limitations 声明由 Layer-C owner 重新落地。
- **P2-5/6（杂物与文档）**：worktree 根 `$null` 临时文件已删；PR_BODY 定稿；测试头注释的基线数字与全量实测（17/18 of 152）对齐。
- **P2-7（Layer-A 重复 id 隐蔽性）**：`AlgorithmMetaStore::loadFromDirectory` 按 id emplace，重复 id 文件对 parity 测试不可见；由 drift 测试的文件数断言兜底——两个 gate 必须同时保留（已注明）。

Reviewer 独立复核确认：四个 gate 在其环境同样全绿、断言计数一致、沙袋证据可复现、窃用 CLAIM 全部实证。
