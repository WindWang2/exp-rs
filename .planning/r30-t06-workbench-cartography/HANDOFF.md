# R30-T06 · HANDOFF（跨轨请求 / 后续工作包）

> 格式：准确路径、当前契约 / 预期契约、最小输入、证据。涉及同一核心实现只有一个 owner。
> 本轨不整支 cherry-pick、不把其他轨实现夹带进 PR。

## H01 → T04（执行/派发权威，仅告知，无需改动）

- 路径：`src/agent/cartography/cartography_operators.cpp`、`src/app/shell/workflow_session_controller.*`、
  `src/agent/output_verifier.*`、通用 OutputCommitter。
- 观察：`cartography_dock.cpp:558/598/626/666/712` 全部通过 `submitOperatorJob()` 走真实
  TaskCenter 异步提交，本轨**保留**该执行路径，未绕过。
- 契约：本轮不修改 operator 派发、线程归属或发布权威；若后续出现 compose/produce 的
  任务终态未回包问题，属 T04。证据：`cartography_dock.cpp:545-730`。

## H02 → T03（数据资产 / collection 后端，条件性）

- 路径：`src/data/**`、`src/app/panels/asset_catalog_index.*`（后者归 T06）。
- 当前契约：`DataManagerPanel::populateCollectionChildren()` 从
  `CollectionSnapshot::childAssetIds` 一次性取全量成员，再用 `AssetCatalogIndex::indexOfAsset`
  做有界索引；`data_manager_panel.cpp:893-937`。
- 预期：F3 需要 collection 的增量/分页成员查询（带 cursor 或 page index），以便
  「所有记录可有界到达」且每次渲染/后台扫描/snapshot 获取都有可说明上界。
- 最小输入：`CollectionSnapshot` 是否需要增量字段由 T03 判断；本轨**不另造** import controller / 存储。
- 证据：`ISSUE_TRIAGE.md C5`、`data_manager_panel.cpp:1130-1158`（standalone 已有分页可复用）。
- 状态：本轮未实施；若 T06 后续恢复 WP06 且确认仅靠现有索引即可实现，则此 handoff 可关闭。

## H03 → T02（模型面板，仅确认边界）

- 本轨不碰模型 readiness 判定；`main_window_workbench.cpp` 模型 UI 只消费现有 typed readiness，
  不解析英文、不等待 T02 增量字段。本轮未改该文件。

## H04 → T05 / T07（共享 UI 宿主）

- `main_window*.cpp/.h` 是共享热点：T05 只接 AgentOps、T07 只接实验/教学、T06 只接通用工作台/地图/制图。
  本轮**未触碰** `main_window_workbench.cpp`（仅只读了 `:140-172, 518-547, 775-800` 作为调用链证据）。
  若后续 WP03 需要改导图来源快照，由 T06 在通用桌面块内最小接线。

## H05 → 本轨后续恢复（非跨轨）

| 工作包 | 准确路径 | 当前 → 预期 | 证据 |
|---|---|---|---|
| WP06 大 collection 分页 | `src/app/panels/data_manager_panel.cpp:893-937` | 单向截断哨兵 → 复用 `m_standalonePage`/pager 的有界分页，可定位超过 cap 的目标 | `ISSUE_TRIAGE.md C5` |
| WP07 制图修订身份 | `src/app/cartography/cartography_dock.cpp:537-542,566-568,496,585,613,687` | `adoptSpec` 在 `compiled` 判定前 / 名称非空即算布局 / 缓存 spec 与控件不同步 → draft/validated/compiled/designer-edited 明确修订 | `ISSUE_TRIAGE.md C3` |
| WP08 多页预览 | `cartography_dock.cpp:766-785` | 只渲染 page 0 → 有界页选择 + 逐页预览 | `ISSUE_TRIAGE.md C6` |
| WP09 交付可检查 | `cartography_dock.cpp:531-535,708-728` | 只显示 JSON / output+page_count → 可读质量条目 + 路径/格式/页数/manifest/checksum + 打开定位 | `ISSUE_TRIAGE.md C6` |
| WP10 项目边界 | 本轨 panel/view/layout | 新项目/另存/关闭/迟到回调矩阵 | `PLAN.md` |
| WP11 实测体验 | 修改界面 | 键盘遍历/窄窗/高 DPI/中文长文本/明暗主题；性能固定输入 | 需真实桌面，Qt offscreen 不足 |
| WP12 三条全链 | 目录→地图 / 任务→结果 / 地图→交付 | 每条至少一个真实入口到真实后端/产物的可离线小场景 | `PLAN.md` |

## 环境问题（非代码债务）

- 全机 `host-build` 锁在启动取证时 `held`（其他 agent）。本轨按纪律等待，未删活锁 / 未终止他人。
  若长期不释放，L11（configure/build）将保持 `BLOCKED`，PR 需标 Draft 并写明 `NOT_EXECUTED`。
