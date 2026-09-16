# PARALLEL_OWNERSHIP — multimodal-registration-11

启动时（2026-09-15）`origin` 只有一个 open PR；#991/#992 已合并。规则按 GOAL Phase 0 执行。

## Open PRs / remote branches

| 来源 | head | 状态 | changed files（摘要） | 与本 track 交集 | 策略 |
|---|---|---|---|---|---|
| PR #1008 | `zcode/radiometric-spectral-workbench` | open | `.gitignore`, `.planning/radiometric-spectral-workbench/*`, `docs/adr/0158-*`, `src/agent/CMakeLists.txt`, `src/agent/spatial_tools/spatial_tool.cpp`, `src/agent/spatial_tools/spectral_spatial_tools.*`, `src/analysis/CMakeLists.txt`, `src/analysis/atmospheric/*`, `src/analysis/hyperspectral/*`, `src/app/CMakeLists.txt`, `src/app/widgets/band_composite_palette.*`, `src/app/widgets/spectral_profile_widget.*`, `src/core/CMakeLists.txt`, `src/core/radiometric_state.*`, `src/core/spectral_library.*`, `src/processing/algorithms/radiometric_calibration.*`, `spectral_indices.*`, `spectral_unmixing.*`, `tests/CMakeLists.txt`, `tests/test_*spectral/radiometric*` | **零业务文件交集**。共享接线文件：`.gitignore`（各 append 一行）、`src/agent/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/core/CMakeLists.txt`、`tests/CMakeLists.txt`、`src/agent/spatial_tools/spatial_tool.cpp`（双方都在 `registerBuiltinTools()` 追加注册行） | 对方 changed files read-only；共享文件只做 append-only 最小 diff；rebase 时保留双方行 |
| `origin/zcode/radiometric-spectral-workbench` branch | 同上 | = PR #1008 head | 同上 | 同上 | 同上 |

## 本 track write scope（收窄后）

- **业务主体（新建）**：`src/processing/algorithms/registration/*`（新子目录，匹配 `*registration*` scope）
- `src/analysis/georeferencing/qgsrpcgcptransformer.{h,cpp}`（D 扩展，additive）
- `src/app/georeferencer/qgsgeoref_shell_window.{h,cpp}`（仅 #1005 fail-closed 修复）
- `src/operators/rs/rs_<new>_operator.{h,cpp}`（新文件；`rs_operators_init.cpp`、`src/operators/CMakeLists.txt`、`src/operators/rs/CMakeLists.txt` append-only）
- `src/agent/tools/geometric_tool.{h,cpp}`（新 action，additive）+ `src/agent/spatial_tools/spatial_tool.cpp`（append 注册）
- `tests/`：新测试文件 + `tests/CMakeLists.txt` append-only
- `docs/processing/geometric-registration.md`（新建）+ `docs/adr/0160-*`（新建）+ CHANGELOG append
- `.gitignore`：append `!.planning/multimodal-registration-11/` 一行

## Read-only（他人所有权）

- `src/processing/algorithms/sar/*`（F01）：只消费 `sar_metadata.h` / `sar_complex.h` 公共契约，不修改
- D18 workbench files（`src/app/workbench/*` 已合并入 master，归 D18 owner；不改动 `georef_dual_window.*`）
- `src/core/*`、spectral/atmospheric/hyperspectral（PR #1008）
- workflow / dataset / io 域（issues #1001–#1004、#1006、#1007 属他人）
