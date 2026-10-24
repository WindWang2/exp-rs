# R21-D OWNERSHIP（写区对账，基线 911e0b17a3）

## 本轨写区（准入）

- `src/app/**` — **排除** `src/app/teaching/**`、`src/app/teaching_admin/**` 及一切教学专属子目录/文件（#1565 即在此排除区）。
- `src/gui/**`（vendored QGIS GUI 基线：修改须最小、可隔离、逐项记录）
- `src/ui/**`
- `src/agent/cartography/**`、`src/agent/layout_tools/**`（改动须配 R20 schema 三腿回归）
- `resources/**` 中 UI 图标/主题/QSS
- `src/recipes/**` 仅制图模板 schema/消费端（与 C 轨同文件时先协调）
- `docs/` 工作台/制图文档
- 对应 UI/制图测试（tests/test_*）
- `.planning/r21-D/**`

## 共享灰区（append-only 最小修改）

- 根 `CMakeLists.txt`（#1623 .qm install：仅 append install 规则块）
- `tests/CMakeLists.txt`、`data` 公共注册表、公共 ADR

## 兄弟轨现场（2026-10-24 盘点）

- R21-A `agent/r21-a-scientific-algorithms-20261023T184835Z`（19d31917d0）— 科学算法域
- R21-B `agent/r21-b-data-model-runtime-20261023T185101Z`（cd4a10eeaf）— 数据/模型/运行时
- R21-C `agent/r21-c-pi-mcp-harness-20261023T185308Z`（142f11cb9e）— Pi/MCP/harness
- R21-D（本轨）
- R21-E 未见 worktree（教学/验证轨可能后建或未启动）

## 红线（不改）

`src/analysis/**`、`src/processing/algorithms/**`、`src/operators/runtime/**`、`src/runtime/**`、`src/data/**`（DataManager 只经 public API 调用）、`src/dataset/**`、`src/geospatial/**`、`src/agent/harness/**`、`src/agent_ops/**`、`src/sdk/**`、`src/workflow/**`、`src/teaching/**`、`src/verification/**`、其他轨 worktree/branch。

## 冲突面预判

- 与 B 轨：`src/data/data_manager.h`（只读使用 acquire/commitEdit/rollbackEdit/hasActiveEditLease 公共 API）；若 B 轨改动签名，本轨以编译期失败为信号再对账。
- 与 C 轨：`src/agent/cartography` schema 三腿（R20 契约），本轨改动时跑 registry 腿回归。
- 与 E 轨：`src/app/teaching/**` 完全让出。
- 根 CMakeLists：#1623 只 append，不动他人区块。
