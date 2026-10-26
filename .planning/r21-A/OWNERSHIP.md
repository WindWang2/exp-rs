# OWNERSHIP — r21-A

## 准入写区（本轨独占）

- `src/analysis/**`（分类/分割/大气/地理配准/高光谱）
- `src/processing/algorithms/**`（科学计算内核：mosaic、SAR、terrain、time、radiometry、enhancement、fusion、spectral 等）
- `src/operators/rs/**`（科学内核算子与参数词汇表）
- `tests/test_*` 中上述域的**同名/同域算法测试文件**（新增测试优先并入既有测试文件；确需新文件时按 `test_r21a_*.cpp` 命名）
- `benchmarks/` 内本轨命名（`r21a_*`）的算法基准
- 领域 ADR：`docs/adr/`（新 ADR 编号 ≥166，先 `ls docs/adr` 核对实际最大号）
- `.planning/r21-A/**`

## 共享灰区（最小 append-only）

- 根 `CMakeLists.txt`、`tests/CMakeLists.txt`、`data/` 公共注册表、公共 ADR。

## 禁改区（他轨所有）

`src/operators/runtime/**`、`src/runtime/**`、`src/data/**`、`src/dataset/**`、`src/geospatial/**`、`src/agent/**`、`src/sdk/**`、`src/workflow/**`、`src/app/**`、`src/gui/**`、`src/teaching/**`、`src/verification/**`、`src/core/**`、`src/processing/framework/**`（除非注册必需且不改跨域语义——本轮预计零触碰）。

## 跨域衔接

- 只经 public interface/contract 衔接；非关键发现 → `CROSS_TRACK_FINDINGS.md`（写明重现、路径、期望所有者）。
- 算法注册/GUI/CLI/MCP schema 一致性 → 只读对账；门面变更交 C 轨，UI 呈现交 D 轨。
