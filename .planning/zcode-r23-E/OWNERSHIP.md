# OWNERSHIP — R23-E 白名单与写区声明

## 允许写入（本轨）

- `src/native/**`
- `src/plugins/host/**`、`src/plugins/framework/**`
- `src/python/**`（仅维护既有可选兼容层；不把 Python 推为核心）
- `cmake/**`、`packaging/**`、工程相关 `scripts/**`
- `.github/workflows/**`
- `CMakePresets.json`（仅直接依赖项）
- 专属集成测试：`tests/test_r23_e_*.cpp` / `tests/test_r23_e_*.sh`（新增文件，append-only 注册块）
- `.planning/zcode-r23-E/**`

## 只读（相邻轨写区，触碰即违规）

- `src/plugins/layer_tree/**`、`src/plugins/processing/**`（C 轨 UI/算法处理插件）
- `src/agent/**`、`src/agent_loop/**`、`src/sdk/exprs/**`、`src/app/**`、`src/processing/**`、`pi/**`、`src/teaching/**`、`src/verify/**`（C）
- `src/geospatial/**`、`src/data/**`、`src/dataset/**`、`src/experiment/**`、`src/operators/runtime/**`、`src/runtime/**`、`models/**`（B）
- `src/processing/algorithms/**`、`src/analysis/**`（A）
- `src/lab/**`、`src/lab_pack/**`、`src/grader/**`、`src/recipes/**`、`src/faultlab/**`、`src/verification/**`、`src/verify_adapters/**`（D）
- `src/core/**`、`src/gui/**`（QGIS vendored，不修改）
- 其他轨生产测试文件（#1699 残余等 → CROSS_TRACK_FINDINGS.md，不改）

## 共享合并面（最小 append-only，三方核验）

- `tests/CMakeLists.txt`：仅文件尾新增本轨注册块（与 R22 各轨同模式）；push 前做 HEAD vs origin/master 注册并集检查（E12 脚本）+ `ctest -N` 证明目标存在。
- 根 `CMakeLists.txt`：仅当 WP4 实机 install 发现缺口时最小修复；预期不触碰。
- `.github/workflows/ci.yml`：仅真缺口最小改；不请求/等待线上运行。

## 特记

- `src/python/scripts/worker_daemon.py`：#1714 备案其对端协议修复跨在 C 轨改动；本轨只在兼容层范围内维护（E04），改动前 diff 复核 #1714 的语义不回滚。
- Windows 侧主 checkout `C:/Users/wangj.KEVIN/projects/exp-rs`：只读（master）。所有写入发生在 WSL 工作树 `/home/kevin/exp-rs-E-20261026T170017Z`。
- 脚本暂存区 `C:/Users/wangj.KEVIN/projects/r23e-scripts/`：仓库外执行辅助（WSL 调用桥），非交付物。
