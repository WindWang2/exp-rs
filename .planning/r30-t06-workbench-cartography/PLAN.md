# R30-T06 · PLAN（内部工作包，按独立迭代管理）

> 不把 goal-loop 的「默认 15 轮」当整轨完成线。每个工作包有独立的完成条件、
> 证据与检查点；按真实完成条件推进，禁止重复相同无效命令消耗预算。

## 迭代与检查点

| 检查点 | 内容 | 状态 |
|---|---|---|
| CP0 | 启动取证、worktree、BASELINE/PR_BRANCH_MATRIX/ISSUE_TRIAGE/PATH_OWNERSHIP/DECISIONS/RESUME | ✅ DONE |
| CP1 | WP01 triage（6 候选逐条源码证据） | ✅ DONE |
| CP2 | RED 测试（当前 HEAD 稳定失败，记录真实输出） | 进行中 |
| CP3 | 生产修复 GREEN | pending |
| CP4 | 双锁 configure/build（-j4）+ ctest -N 核对真实测试名 | pending（host 锁 held，待 free） |
| CP5 | 独立 R1 → fix → R2（绑定最终 HEAD） | pending |
| CP6 | 记账 / 用户文档 / PR（仅推自己分支，不合并） | pending |

## 工作包清单（任务 §七 WP01–WP12）

| WP | 内容 | 本轮状态 | 说明 |
|---|---|---|---|
| WP01 | 确认已合并基线与真实桌面缺口 | **DONE** | `ISSUE_TRIAGE.md`，6 候选逐条源码证据；R29/#2044 已合入项标 `ALREADY_DONE_WITH_EVIDENCE` |
| WP02 | 修通图层组操作与活动 canvas | **本轮实施** | C1 / F1 |
| WP03 | 当前视图→制图输入完整身份 | `NEEDS_REPRO` | C4；需第二视图/同 source 不同样式真实 GUI 场景，不凭字符串断言 |
| WP04 | 历史按钮/菜单/比较同一动作资格 | **本轮实施** | C2 / F2 |
| WP05 | 重试反馈、刷新和生命周期收尾 | **本轮实施（部分）** | C2 的 resubmit 失败 ID + 触发前重查；排序/多选/刷新风暴/重开面板的完整矩阵留后续 |
| WP06 | 大 collection 有界可访问性 | 后续（C5 已确认） | collection children 缺分页入口；复用 `AssetCatalogIndex` + standalone pager 模式 |
| WP07 | 制图草稿/编译/失败/导出同一修订 | 后续（C3 已确认） | `adoptSpec` 在 `compiled` 判定前、名称非空即算布局、缓存 spec 与控件不同步 |
| WP08 | Designer 接续、修复差异、多页预览 | 后续（C3/C6 已确认） | `updatePreview` 只渲染 page 0 |
| WP09 | 交付物可检查且反馈准确 | 后续（C6 已确认） | 报告仅 JSON、produce 仅 output/page_count |
| WP10 | 项目边界与既有控件契约 | 后续 | 需 ProjectContext/SessionBoundary 场景矩阵 |
| WP11 | 实测体验与性能 | 后续 | 高 DPI/焦点/渲染结论需真实桌面；本轮只做 Qt offscreen 生命周期与状态回归 |
| WP12 | 三条完整链 + 独立 review + PR | **部分** | 本轮 PR 只覆盖 WP02/WP04/WP05 的链；三条完整全链留后续恢复，不冒充完成 |

## 本轮可交付的「用户可观察行为」

| 行为 | 判定方式 |
|---|---|
| 组缩放作用在**当前活动 canvas** 且不会因空 canvas 崩溃 | offscreen Qt 测试：构造真实 `QgsMapCanvas` + `QgsLayerTreeGroup` + `ActiveViewHost`，检查菜单 action 绑定与 enabled；触发后断言 `canvas->extent()` 变为组联合 extent |
| 空组 / 纯属性表 / 无效图层 / canvas 被删 / 视图切换 → 诚实拒绝 | action 被 disable 且 tooltip 有原因；触发时 extent 不变且无解引用 |
| 历史右键/键盘与按钮资格一致 | 同一 `ActionPlan`；终态不可 cancel、running 不可 rerun、无产物不可 open/inspect |
| 两条历史各能选**非首产物**比较 | 两侧各一次 `pickOutputArtifact`，`compareRequested` 携带用户实际选择；缺失/跨类型/取消不 emit |
| 失败提交不显示 queued | `retryTask==0` 且 `enqueueTask<=0` 时状态栏为失败说明，不含 "queued" |

## 真实测试入口（先 `ctest -N` 核对 prefix/数量）

新增：`test_r30_t06_group_zoom`（`tests/test_r30_t06_group_zoom.cpp`）、
`test_r30_t06_history_action_parity`（`tests/test_r30_t06_history_action_parity.cpp`）。
回归（只运行、不改）：`test_r29_t04_history_actions`、`test_processing_history_model`、
`test_r22_workbench_view_tree`、`test_layer_tree_bridge`、`test_data_manager_panel`。
目标名 ≠ CTest case 名；`sicnu_discover_tests` 会生成 Catch2 case，必须核对 `--show-only=json-v1`。

## 资源纪律

- 双锁前缀：`python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 45 -- python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 45 -- <payload>`
- 构建 payload 显式 `--parallel 4`；测试 payload 显式 `ctest -j1`，guard 层 `--parallel 1 --max-parallel 4`。
- 禁止 `-j$(nproc)` / 无上限 `-j`；内存压力时降为 `-j2/-j1` 并同步调 guard。
- 环境：`QT_QPA_PLATFORM=offscreen`、单线程 BLAS/OMP。
