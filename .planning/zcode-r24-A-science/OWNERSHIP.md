# R24-A Ownership（唯一写集）

## 本轨独占写

- `src/processing/algorithms/**`（科学算法内核与算子实现）
- `src/analysis/{atmospheric,classification,georeferencing,hyperspectral,segmentation}/**`
- `src/operators/rs/**`（科学算子接线面，非 runtime/）
- `data/processing/algorithm_meta/`（与上述算法直接关联的单个 sidecar，仅确需时）
- `tests/test_r24_a_*.cpp`（本轨新测试，唯一前缀）
- `tests/CMakeLists.txt`：**仅末尾 append `# BEGIN R24-A` … `# END R24-A` 独立注册块**，不动兄弟块
- `.planning/zcode-r24-A-science/**`
- 例外最小桥（如确需）：见 DECISIONS.md 逐条记录原因与 diff

## 共享只读（默认不动）

- `src/core/`、`src/gui/`（QGIS vendor）、根 CMakeLists、`src/sdk/`、`src/plugins/`、`src/teaching*`、`src/processing/framework/**`、`src/processing/gdal/**`、`src/processing/providers/**`（help 目录键修复走 `src/processing/algorithm_help_catalog.*` 算法元数据面，属 WP-A7 算法级帮助，不属 GUI）
- `src/operators/runtime/**`、`src/geospatial/**`、`src/dataset/**`、`src/experiment/**`（B 轨）
- `src/agent/**`、`src/app/**`、`pi/**`（C 轨）
- `.goal-loop-ledger.md`（全仓共享账本，不写；本轨用 `.planning/zcode-r24-A-science/LOOP_LEDGER.md`）
- 主仓与兄弟 worktree 的一切内容

## 账本重定向声明

goal-loop 技能默认读写 `<工作区>/.goal-loop-ledger.md`；该文件为全仓共享、跨轨冲突面。本轨将 goal-loop 读写**重定向**至 `.planning/zcode-r24-A-science/LOOP_LEDGER.md`（R24-A 专属），不创建/不修改共享账本。
