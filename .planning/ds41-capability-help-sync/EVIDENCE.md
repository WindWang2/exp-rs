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

（待填写：两次 gen-meta + gen-pages --check 输出，第二遍后 `git status --short data/processing pi/knowledge` 为空）

## O-tamper：漂移 gate 双向实证

（待填写：篡改 rs-ndvi.json summary → test_capability_knowledge/test_capability_completeness FAIL 输出；git checkout 恢复 → PASS 输出）

## O-parity：surface parity gate

（待填写：test_capability_surface_parity 全量输出 + CLI 子进程调用日志）

## O-completeness：完整性 gate

（待填写：test_capability_completeness 输出 + NoData census WARN 行 + 豁免清单核对结果）

## O-double-run：关键 gate 连续两遍

（待填写：4 个测试二进制 × 2 轮的 exit code 与汇总）
