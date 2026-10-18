# R16-E Ownership（文件互斥合同）

## 轨道 09（R16-E-T09，help + i18n）—— 可写

- `src/help/**`，**除** `src/help/operator_help_provider.cpp`（#1517 未合并改动拥有；只读）
- `data/help/**`，**除** `data/help/operators/data_io.json`、`data/help/operators/optical.json`（#1517 拥有；只读）
- `resources/translations/**`
- `scripts/audit_i18n_strings.py`
- 新测试文件：`tests/test_r16_E_help_*.cpp` / `tests/test_r16_E_i18n_*.cpp`
- `tests/CMakeLists.txt`：仅追加本轨道测试注册（最小增量，文件末尾附近插入，禁止重排既有行）
- `.planning/r16-e-t09/**`

## 轨道 10（R16-E-T10，build/ctest/packaging）—— 可写

- `CMakeLists.txt`（含 lupdate 范围行——按 T09 审计规格执行的最小 diff）
- `tests/CMakeLists.txt` 中**非注册性**的构建/守卫改动（与 T09 的追加注册不重叠；如必须同区编辑，由集成阶段串行处理）
- `tests/sicnu_test_main.cpp`、`tests/support/**`（fixture 健壮性）
- `scripts/check_test_registration.py`、`scripts/check_presets.py`、`scripts/verify_bundle_manifest.py`、`scripts/report_bundle_dependencies.py`、`scripts/build_offline_bundle.sh`、`scripts/build_offline_bundle.cmd`、`scripts/offline_smoke.sh`、`scripts/bundle_manifest.ps1`（构建/打包校验类脚本）
- `packaging/**`、`.github/workflows/**`（仅既有 CI 语义修复）
- `CMakePresets.json`、`cmake/**`
- 新测试/工具：`tests/test_r16_E_build_*.cpp` 或 scripts 侧
- `.planning/r16-e-t10/**`

## 双方只读

- 所有 `src/app/**`、`src/teaching_admin/**`、其它 src 模块（交叉模块仅读）
- 其它 16 个本地 worktree 与全部非 R16-E 分支
- #1513–#1517 五个开放 PR 的改动文件（除上表明确允许的最小增量外）

## 集成（orchestrator）

- `hardening/r16-e-help-delivery` 集成分支：cherry-pick 两轨提交（保留来源 SHA 引用）、冲突解决、`.planning/r16-e/**` 共享文档、PR 正文。
