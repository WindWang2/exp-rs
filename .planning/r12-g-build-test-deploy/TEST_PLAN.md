# R12-G TEST PLAN — 分层验证阶梯（L0 → L4）

- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test` · 分支 `hardening/r12-g-build-test-offline`
- **资源纪律（全程）**：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`
- **分层原则**：低层**无需工具链**即可跑（hermetic），在没有 build 树、没有网络、没有 vcpkg 引导的机器上也拿得到证据；高层才触碰真实构建。

## 1. 阶梯总览

| 层 | 名称 | 需工具链 | 需 build 树 | 命令 | 预期 | 本轮状态 |
|---|---|---|---|---|---|---|
| **L0** | 静态 / 语法 | ❌ | ❌ | `bash -n scripts/build.sh` · `python scripts/check_presets.py CMakePresets.json` | 全部 exit 0 | ⚠️ **PARTIAL** |
| **L1** | hermetic selftest | ❌ | ❌ | `bash scripts/build.sh selftest` · `python scripts/verification_ladder.py --list` | `selftest: ALL PASS`（含租约断言） | ✅ **PASS** |
| **L2** | 真实 configure | ✅ | ❌（自生成） | `cmake --preset dev-default -DVCPKG_MANIFEST_MODE=OFF` | `Configuring done` + `Build files have been written` | ⚠️ **PARTIAL** |
| **L3** | 真实编译 | ✅ | ✅ | `bash scripts/build.sh build --build-dir build-dev --jobs 2` | 含 netcdf 的 3 个 target 链接成功 | ❌ **NOT_RUN** |
| **L4** | `ctest -j1 offscreen` | ✅ | ✅ | `bash scripts/build.sh test --build-dir build-dev` | 全注册通过 | ❌ **NOT_RUN** |

## 2. L0 — 静态 / 语法层 ⚠️ PARTIAL

### 2.1 ✅ CMakePresets 卫生门禁（真实执行）

```sh
python scripts/check_presets.py CMakePresets.json
```

**真实输出（节选，全 30 项 `ok`）**
```
check_presets: CMakePresets.json
  ok: presets version is 3
  ok: cmakeMinimumRequired declared
  ok: configure preset names unique
  ok: every configure preset has binaryDir
  ok: every configure preset is described
  ok: binaryDir is ${sourceDir}-relative (no absolute paths)
  ok: an offline/lab profile preset exists (SICNU_LAB_PROFILE=ON)
  ok: a Debug dev profile exists
  ok: a Release CI profile exists
  ok: build presets exist
  ok: build preset 'dev-default' references an existing configure preset
  ok: build preset 'dev-default' pins jobs within 1..2
  ok: build preset 'offline-lab' pins jobs within 1..2
  ok: build preset 'ci-fast' pins jobs within 1..2
  ok: build preset 'ci-full' pins jobs within 1..2
  ok: build preset 'sanitizer-debug' pins jobs within 1..2
  ok: build preset 'release-package' pins jobs within 1..2
  ok: test presets exist
  ok: test preset 'dev-default' pins ctest -j1 (CTEST_PARALLEL_LEVEL)
  ok: test preset 'dev-default' runs offscreen
  ok: test preset 'dev-default' reports failures verbatim
  ... (ci-fast / ci-full / sanitizer-debug 各 4 项同构)
check_presets: ALL PASS
EXIT=0
```
**统计**：30 项断言全部 `ok`；覆盖 6 个 configure / 6 个 build / 4 个 test preset。

### 2.2 ❌ 未以命令原文执行的部分（如实记录）

| 命令 | 状态 | 说明 |
|---|---|---|
| `bash -n scripts/build.sh` | ❌ **NOT_RUN** | 本轮未单独跑；selftest 已完整执行该脚本（覆盖更强） |
| `python -m compileall -q scripts/` | ❌ **NOT_RUN** | 本轮未跑；三个 py 脚本均已被真实 import 并执行（覆盖等价） |

> 未以命令原文执行过，故不记为 PASS。

## 3. L1 — hermetic selftest 层 ✅ PASS

**目标**：在**没有工具链、没有 build 树、没有网络**的机器上也能证明资源纪律与可移植性契约成立。

### 3.1 `bash scripts/build.sh selftest`（真实输出，完整）

```
build.sh selftest: resource caps + path/space/Unicode quoting
  ok: default jobs = 1
  ok: jobs 1
  ok: jobs 2 (cap)
  ok: jobs 3 refused with exit 2
  ok: jobs 8 refused with exit 2
  ok: inherited CMAKE_BUILD_PARALLEL_LEVEL=8 clamped to cap
  ok: inherited CTEST_PARALLEL_LEVEL clamped
  ok: space/Unicode args stay one argument each
  ok: argument values preserved verbatim
  ok: wrapper runs from spaced/Unicode cwd
  ok: repo root with spaces resolves
  ok: build command pins --parallel 2
  ok: build command never exceeds cap
  ok: ctest pinned -j1
  ok: ctest sets offscreen platform
  ok: busy build dir refused (exit 75); payload never ran
selftest: ALL PASS
EXIT=0        (real 2m3s)
```
**GREEN2（第二轮独立复核，日志 `/tmp/r12g-selftest-final.log`）**：同样 `selftest: ALL PASS` / `EXIT=0`，16 项断言一致。

| 组 | 断言数 | 内容 |
|---|---|---|
| 1 | 8 | `-j` 上限拒绝（exit 2）、继承环境变量钳制、Unicode/空格参数不分裂 |
| 2 | 3 | 从含空格/Unicode 的 cwd 运行、repo root 含空格可解析 |
| 3 | 4 | build 命令固定 `--parallel 2` 且不超上限、ctest 固定 `-j1` + offscreen |
| **4（本轮新增，G06）** | **1** | stub cmake + 后台持锁 + `--wait 0` → 必须 exit 75 且 payload 未跑 |

### 3.2 `python scripts/verification_ladder.py --list`

**真实输出**：列出 `L0 compile-guards` → `L8 full-sweep`，共 **10 条 lane**（含可选 `LS sanitizer`）、**61 个 item**，exit 0。默认 lanes = `L0,L1,L2,L3,L4,L5,L7`（`:377`），排除 `LS/L6/L8`。

### 3.3 ⚠️ L1 暴露的真实失败：resource_guard 单元测试 1 项不通过

详见 `REPRODUCERS.md` R8：工作区重跑 ×3 失败 3/3；`git show HEAD:` 导出**原版**后同样失败 → **先于本轮改动存在，非 R12-G 回归**（诱因：本机 `python -c "pass"` 启动 ≈ 0.95 s > payload `sleep(0.6)`）。**不影响 G06 的 PASS 判定**（G06 的 oracle 是 `probe_g06_guard.sh` 与 selftest 第 4 组，均绿）。未修。

## 4. L2 — 真实 configure 层 ⚠️ PARTIAL

**命令**
```sh
cmake --preset dev-default -DVCPKG_MANIFEST_MODE=OFF \
      -DCMAKE_PREFIX_PATH="C:/deps/Qt/6.8.0/msvc2022_64;.../exp-rs/build-dev/vcpkg_installed"
```

**✅ 已得证据（`build-logs/r12g-configure5.log`，真实记录）**
```
-- The CXX compiler identification is MSVC 19.38.33130.0
-- Found OpenCV 4.12.0 ... · -- Found QCA: C:/deps/qca-install/lib/qca-qt6.lib (2.3.10)
--   NetCDF: provided through GDAL drivers (runtime audit: sicnu_geo_rs_cli env-doctor)
-- Configuring done (397.5s)
-- Generating done (103.6s)
-- Build files have been written to: .../exp-rs-r12-g-build-test/build-dev
=== EXITCODE: 0 ===
```

| 阶段 | 耗时 |
|---|---|
| Configure | **397.5 s**（≈ 6 m 38 s） |
| Generate | **103.6 s**（≈ 1 m 44 s） |
| **合计** | **501.1 s**（≈ 8 m 21 s） |

**⚠️ 时间线缺口（如实记录）**

| 事实 | 证据 |
|---|---|
| `build-dev/CMakeCache.txt` mtime | `Oct 14 01:25` |
| `tests/CMakeLists.txt` 本轮改动（新增 `find_path`） | 晚于 01:25 |
| `grep NETCDF_INCLUDE_DIR build-dev/CMakeCache.txt` | **无命中** |

→ **397.5 s 是改动「之前」的 configure 证据**；G05-1 的 GREEN2（重跑 configure）= ❌ **NOT_RUN**。

**未重跑的原因**：① G05-2 未收敛（`:423` 注释自称 optional、`:428` 实现是 `FATAL_ERROR`，而 `vcpkg.json` 无 netcdf），先重配会把"可选依赖缺失"变成硬阻塞；② 单次 configure ≈ 501 s，须与并发 agent 的构建租约排队。

**早期 configure 失败台账（环境引导，非代码缺陷）**

| 日志 | 退出码 | 失败原因 |
|---|---|---|
| `r12g-configure.log` | 1 | vcpkg manifest 联网引导失败（`CreateFileW stdin failed with 231`）+ 连锁 `CMAKE_MAKE_PROGRAM not set` |
| `r12g-configure2.log` | 1 | `vcpkg manifest mode was disabled for a build directory where it was initially enabled` |
| `r12g-configure3.log` | 1 | MSVC 试编译失败：`rc.exe` 找不到 |
| `r12g-configure4.log` | 1 | `rc.exe` / `mt.exe` 不在 PATH（缺 Windows SDK bin） |
| `r12g-configure5.log` | **0** | 成功（397.5 s） |

→ 证明干净 Windows lane 的阻塞点**全在环境引导**，不在仓库代码。

## 5. L3（真实编译）· L4（`ctest -j1 offscreen`）❌ NOT_RUN

**命令**
```sh
bash scripts/build.sh build --build-dir build-dev --jobs 2
bash scripts/build.sh test  --build-dir build-dev          # 内部固定 -j1 + offscreen
```

**预期（修复后应看到）**：`test_io_multidim` / `test_io_subdataset_inventory` / `test_io_fabric_multidim_11` 三个含 `netcdf.h` 的 target **编译期不再报 `fatal error: netcdf.h: No such file or directory`**（这正是 G05-1 要消灭的失败形状）。

**未执行的原因**：① `build-dev` 是**已 configure、零 .exe 产物**（`ls build-dev/tests/*.exe | wc -l` = 0），做 L3 即重型 C++/Qt/QGIS 全量编译；② 本轮硬约束禁止重型编译；③ 前置：G05-1 的 GREEN2（重配）未做，缓存尚无 `NETCDF_INCLUDE_DIR`。

## 6. 跨层门禁：验证阶梯自身（G12）

本轮修了两个"静默假绿"缺陷并已取证，完整输出见 `REPRODUCERS.md` R3 / R4：

| 子项 | 修复位置 | GREEN 证据 |
|---|---|---|
| **G12-2** 空注册判 passed | `:377-378`（追加 `--no-tests=error`）、`:348-363`（能力探测）、`:386-393`（文本兜底映射） | `supports --no-tests: True` / `STATUS = failed` / `DETAIL = ctest matched no test for pattern '.*': …` |
| **G12-1** 零执行算 PASS | `:478-483`（计数器）、`:604-613`（聚合判负） | `executed_items=0 planned_items=4` → `OVERALL: ATTENTION` / exit 1 |

**为什么必须做能力探测**：`--no-tests` 是 CMake ≥ 3.18 才有的选项。硬加会让**所有** ctest lane 在老 CMake 上退化成 usage error——把"防假绿"变成"全红"，是更坏的静默失败（症状掩盖原因）。探测一次并缓存在 `_CTEST_NO_TESTS_SUPPORTED`，失败即优雅降级；即使降级，`No tests were found` 的文本兜底仍生效。

**未修**：G12-3（P3）—— `--resume` 对 `kind=="target"`（L0 `sicnu_header_probes`）不做身份校验，`:473-475`、`:539-547`。

## 7. 反作弊红线落点

| 红线 | 保障 |
|---|---|
| 不得删除/跳过测试 | 本轮**零测试删除**；G04-1 的 41 个条件门一个没改；G02 的 20 个 `_exit` 一个没动（DECISIONS.md D1） |
| 不得把"没跑"记成"通过" | G12-1（`executed_items==0 → 失败`）+ G12-2（`--no-tests=error`），均已取证 |
| 不得伪造证据 | 本目录所有文档只登记真实执行过的命令；未跑的一律 `NOT_RUN` |
| 不得越权改他人方向 | `src/**` 完全不碰；`tests/CMakeLists.txt` 只读短租（OWNERSHIP.md） |
