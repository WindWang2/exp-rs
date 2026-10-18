# R16-E T10 IMPLEMENTATION — 改动明细

Base 46791bc65 → 本轨 4 个提交（每 WP 一个，见 `git log`）。未触碰任何 `src/**`。

## Commit 1 — WP4 `build(i18n): scan src/python in sicnu_i18n_update lupdate scope (#1489)`

- `CMakeLists.txt` lupdate 范围行（原 :973）：目录清单尾部追加 `src/python`。
- 已核实唯一漏扫串：`src/python/sicnu_python_console.cpp:45` `tr("Enter Python code...")`。
- 验证（lupdate 6.12.0，`-no-obsolete -locations none`，输出 /tmp 新 .ts，不改 tracked .ts）：
  - 基线 13 目录：23594 strings / 1572 contexts / `Enter Python code` 命中 0。
  - +src/python：23595 / 1573，新 context `SicnuPythonConsole`，唯一差异即该新条目。

## Commit 2 — WP2 `test(census): walk tests/ subdirectories for orphan test_*.cpp (#1494)`

`scripts/check_test_registration.py`：
- `collect_test_sources`：`os.listdir`（仅顶层）→ `os.walk` 递归；输出相对路径
  （`support/test_x.cpp`），ORPHAN 行直接点名位置。
- `cmake_text`：追加读取子目录 `CMakeLists.txt`（注册可写在 `tests/support/CMakeLists.txt`；
  现存两个 fixture 项目无 test_*.cpp 引用，纳入无害）。
- stem 匹配改为 basename（`support/test_x.cpp` 与裸 `test_x.cpp` 两种注册写法都命中）。
- compile_commands 比对走 `os.path.realpath`（子目录符号链接安全）。

## Commit 3 — WP3 `packaging(bundle): record and verify the executable bit (#offline-integrity)`

- `scripts/build_offline_bundle.sh` manifest 写入器：payload 文件带任一 x 位 → 追加
  `"exec": true`（additive；合成 bundle 证明：755 → true，644 → 无字段）。
- `scripts/verify_bundle_manifest.py`：POSIX 上声明 `exec:true` 而文件无 x 位 →
  finding `exec bit lost: <path>`。仅字面量 True；仅声明时校验（旧 manifest 行为不变）。
- `tests/fixtures/bundle_manifest/conformance.py`：新增 `scenario_exec_bit`（POSIX-only，
  非 POSIX 打印 not-executed），RED→GREEN 证明见 TEST_MATRIX。
- `packaging/OFFLINE_BUNDLE.md`：契约段落（字段语义、兼容性、PS 侧忽略）。

## Commit 4 — WP5 `build(presets): assert configure-preset binaryDir uniqueness`

`scripts/check_presets.py`：configure presets 的 binaryDir 值唯一性断言
（原先只查存在 + ${sourceDir}-相对；两个 preset 共享 build dir 会互相改写 CMakeCache）。

## Commit 5 — WP6 + 台账（本提交）

`.planning/r16-e-t10/**`：台账、测试矩阵、性能记录、CI 快照证据（git add -f）。

## 刻意不改（协调项）

- `src/geospatial/vector/**`（#1513 归属）——WP1 结论本来也无需改产品代码。
- `tests/CMakeLists.txt`：本轨零改动（census/超时审查均无需注册性或守卫性改动；
  尾部区域完全未触碰，与 T09/#1515/#1517 的追加式改动无交集）。
- `.github/workflows/ci.yml`：只读复核，语义未动。
- PS 孪生 `bundle_manifest.ps1`：静态审查记录两处（exec 字段忽略=设计；`data/../manifest.json`
  别名条目 PS 不拒=对等性缺口，无 pwsh 不修）。
