# R16-E T10 TEST_MATRIX — 真实运行记录（无虚报）

工具链：cmake 3.30.5 · Ninja 1.12.1 · gcc 16.2.1 · GDAL 3.13.3（pwb-sdks prefix）·
Catch2 v3.7.1（离线缓存）· lupdate 6.12.0 · ctest 3.30.5（~/.local/opt）。
构建目录：`build-r16-e-t10`（Ninja/Debug；SICNU_BUILD_OTB/EMBED_PYTHON/OPENVINO/
TENSORRT/VENDOR_GDAL/WITH_ONNX_RUNTIME=OFF；SICNU_TOLERANT_TEST_DISCOVERY=ON）。

## WP1 — #1491 本地裁决

| 步骤 | 命令 | 结果 |
|---|---|---|
| 配置 | `cmake -S . -B build-r16-e-t10 -G Ninja ...`（flock 持锁） | 第 1 次失败：GDAL 未发现（缺 prefix）→ 加 `-DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`；第 2 次失败：Catch2 FetchContent 走不可达代理 → 加 `-DFETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1`；第 3 次 `Configuring done`（见 PERFORMANCE） |
| 目标构建 | `cmake --build build-r16-e-t10 --target test_io_vector_contract --parallel 4`（flock） | 176/176 成功（~2 分钟，io 套件只链 Sicnu::Geospatial，无 qgis_core 依赖链） |
| 全套运行 | `QT_QPA_PLATFORM=offscreen ctest -j1 -R io_vector_contract --output-on-failure` | **`100% tests passed, 0 tests failed out of 14`**（FAST 2.29s*proc，real 2.43s） |
| :352 精确用例 | `ctest -j1 -R "unknown fields and malformed" -V`（:352 属 tests/test_io_vector_contract.cpp:339 的 TEST_CASE） | **1/1 Passed** |
| CI 半边（证据） | 基线 SHA 的 push run 38843229045 Tier1 失败日志 | `test_io_vector_contract` 14 Passed / 0 Failed；该 run 总计 `99% tests passed, 56 tests failed out of 8837`——失败全在其它套件（evidence/tier1-io-vector-14-passed.txt） |

**结论 (b)：master 已不复现（本地 + CI 双重证据）。**

## WP2 — census 守卫

| 场景 | Before | After |
|---|---|---|
| 合成孤儿 `tests/support/test_hidden_orphan.cpp`（mktemp 树，临时已删） | `census: 1 test_*.cpp; 0 orphan(s)` exit 0（**盲区：不可见**） | `ORPHAN: support/test_hidden_orphan.cpp`，exit 1 |
| 真实树 static 模式 | 1007 / 6 template-ok / 0 orphan，exit 0 | 同（递归化零误报，计数不变） |

## WP3 — 离线交付完整性

| 场景 | 结果 |
|---|---|
| conformance 基线（18 检查） | 全 ok（PS lane not-executed：无 pwsh） |
| 同尺寸篡改（单字节翻转，字节数不变） | `sha256 mismatch data/samples/a.tif`，exit 1（检测有效） |
| exec RED（conformance scenario_exec_bit 剥离半边，修复前） | `FAIL: exec bit stripped fails despite matching bytes BUNDLE VERIFY PASS` |
| exec GREEN（修复后全套） | **20 checks, 0 failed**；剥离半边 → `exec bit lost: RUN.sh` exit 1 |
| builder 写入器合成证明（直接执行 heredoc python） | `bin/cli`(755) → `"exec": true`；`README-zh.md`(644) → 无字段；strip 后 verify 出 `exec bit lost: bin/cli` |
| 语法 | `sh -n build_offline_bundle.sh` OK；heredoc python + 两个校验脚本 `py_compile` OK |

## WP4 — lupdate

| 运行 | strings | contexts | unfinished | `Enter Python code` |
|---|---|---|---|---|
| 基线 13 目录 → /tmp/fresh-base.ts | 23594 | 1572 | 23594 | 0 命中 |
| +src/python → /tmp/fresh-python.ts | 23595 | 1573 | 23595 | 1（context `SicnuPythonConsole`） |

差异 = 恰好 1 条新串（fresh .ts 全量 unfinished 属新生成文件的定义，非回归）。tracked .ts 未动。

## WP5 — presets 与超时治理

- `check_presets.py`：44 ok / ALL PASS（改动前）；改动后真实树仍 ALL PASS + 新断言。
- binaryDir 唯一性 RED/GREEN：合成双 preset 共享 `${sourceDir}/build-x` → 修复前 ALL PASS
  （盲区）/ 修复后 `FAIL: configure preset binaryDirs unique` + `FAILED (1)`。
- 超时审查（只读证据，无需修）：`http_range_server.cpp:59-65` SO_RCVTIMEO=2s；
  `http_s3_server.h:80-87` 2s；`http_stac_server.cpp:207` 复用 `boundSocketWait`（2s）；
  `mini_cog_server.h:102-104` 30s（#1468）。无 DART_TESTING_TIMEOUT 显式设置 → ctest 默认
  1500s/用例兜底；178 处显式 TIMEOUT 注册在案。

## WP6 — CI 只读

- `ci.yml:9-21` #1490 SHA 分组、`ci.yml:115-118` #1495 阻断式 clang-tidy：在位确认。
- `ci_master_status.py --runs evidence/t10-runs.json --jobs-dir …`：
  `runs=25 counts={'cancelled': 10, 'failed-unknown': 15}`；
  latest completed push→master = **failed-test**（id 38843229045, sha 46791bc6）；
  失败步骤：Tier1 `Execute Fast CTest Suite`、Tier2 `Execute Sanitizer CTest Suite`、
  macOS `Verify macOS Build`（failed-build）。`--gate` → FAIL（exit 1）。
