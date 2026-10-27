# R24-B OWNERSHIP

## 唯一写权限

- `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`
- `src/operators/runtime/**`、`src/runtime/**`
- `models/**`（仅真实验证所需清单文件）
- `tests/test_r24_b_*.cpp` + `tests/CMakeLists.txt` 末尾独立 `# BEGIN R24-B` … `# END R24-B` 注册块（唯一前缀 `test_r24_b_`）
- `.planning/zcode-r24-B-data-model/**`

## 只读（默认）

- `src/core/`、`src/gui/`（QGIS vendor）、根 `CMakeLists.txt`、`src/sdk/`、`src/plugins/`、`src/teaching*/`、`src/agent/**`、`src/app/**`、`pi/**`、`src/processing/algorithms/**`（A 轨）、`src/preflight/**`、`src/science_context/**`、`src/operators/framework/model_catalog.*`（如必须改需碰撞检查）
- 共享全局账本 `.goal-loop-ledger.md`：不改写不提交；本轨账本 = 本目录 `LOOP_LEDGER.md`
- 主工作树与兄弟 worktree（`../exp-rs-D-20261026T165151Z`）全部只读
- `data/samples/manifest.json`（主树未跟踪文件）：不碰

## 需要触碰共享面时

`tests/CMakeLists.txt` 只允许**末尾 append** 自带 BEGIN/END 标记的注册块；不动兄弟块。若发现编译阻塞必须改非 OWNED 文件：最小桥接 + 记录 `DECISIONS.md` + PR 说明。
