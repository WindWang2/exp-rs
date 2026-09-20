# DEDUP — ds41-capability-help-sync

执行环境离线（`git fetch` 失败，`gh` 不可用）。去重基于：本地 refs（2026-09-20 fetch 状态）+ master 提交历史 + 远端分支三点比对 + 已合并 track 的 planning 文档。

## 远端分支判定（全部 behind=80）

| 分支 | ahead/behind | 判定 |
|---|---|---|
| `origin/agent/ds41-http-fetch-strict` | 2/80 | 历史残影（http strict 域），不覆盖本 Track |
| `origin/agent/ds41-pipeline-drag-lifetime` | 4/80 | 历史残影（生命周期域） |
| `origin/agent/flash-*` ×5 | 4-8/80 | 已合入 track 残影（data-transaction/fabric/foundry/mcp-routing/atomic-errors/workflow） |
| `origin/agent/glm53-*` ×2 | 12/7/80 | 已合入 track 残影（desktop-lifecycle/plugin-sdk-trust） |
| `origin/fix/*` ×3 | 1-2/80 | 已合入修复残影 |

结论：**无任何现存分支覆盖本 Track 范围**；抽样确认这些分支的 capability sidecar 内容与 master 同源或更旧，不 cherry-pick。

## 已合并 track 重叠判定

| Track | PR | 重叠面 | 判定 |
|---|---|---|---|
| `cli-mcp-agent-surface-11` | #1020 已合并 | CLI/MCP/Pi **tool 面** projection parity（`test_surface_parity.cpp`） | **部分重叠**：本 Track 的 surface parity 只做 **algorithm/capability 面**，并扩展其测试文件模式；不重做 tool 面。该 Track 的 known limitation（legacy CLI 双 parser/双 schema 格式、`source` 词汇表不一致）由本 Track 登记为 parity gate 的文档化豁免/known limitation |
| `unified-help-diagnostics-6` / `context-help-diagnostics-11` | 已合并 | F1/HelpCenter/diagnostics/i18n/命令页 | **不重叠**：那是交互帮助系统；本 Track 只管 capability/operator help 的**生成与完整性**，通过生成物/测试接口协作（operator_help_provider 只读） |
| Scientific Verification（contract census，#1097 wave-2） | 已合并 | 科学契约 census | **相邻不重叠**：本 Track 只负责用户可见 metadata/help surface；feasibility 知识 `data/agent/capabilities` 只读并交叉验证 |

## 与 open PR/issue 的关系

离线无法查询实时 open PR/issue。启动快照为 0/0。开始实现前若出现新 PR 触碰 `data/processing/**` 或新增 capability 测试，按 Track 规则自动缩小到相邻缺口并记录，不复制实现。
