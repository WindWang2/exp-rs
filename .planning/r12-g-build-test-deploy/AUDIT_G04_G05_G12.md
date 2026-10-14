# AUDIT_G04_G05_G12 — 只读审计（G05 可选依赖精准失败 / G04 CTest 注册隔离与漂移 / G12 轻量分层验证梯队）

- **审计者**: general-purpose-3（只读，零文件写入；本报告为唯一写入产物）
- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`
- **分支/基线**: `hardening/r12-g-build-test-offline` @ `28abef358`（= origin/master，工作区 `git status` 干净）
- **核查时间**: 2026-10-14
- **硬约束遵守情况**: 全程只读；未执行任何 `cmake --build` / 未创建 build 目录 / 无 git 写操作；仅执行了 3 个纯只读脚本（`scripts/check_presets.py`、`verification_ladder.py --list`、若干 inline python census 只读扫描）。
- **环境事实**: 本 worktree **无 build 目录**（未 configure），故 `ctest -N` 不可得；`cmake/ctest` 不在 PATH，Qt 自带 `C:/Qt/Tools/CMake_64/bin/cmake.exe`（3.29.3）/ `ctest.exe` 可用。

---

## 1. 已核实事实（逐条对照 #1468 线索，以代码为准）

### 1.1 #1468 · `tests/CMakeLists.txt` unguarded netcdf `find_library` → **ALREADY_FIXED（且被过度硬化）**

现场代码：

```cmake
# tests/CMakeLists.txt:423-437
# #1468: netcdf is an optional DRIVER dependency, but these suites link it
# directly. A bare find_library leaves the NOTFOUND literal in
# target_link_libraries and the io targets fail at LINK time with cryptic
# errors — refuse at configure time with an actionable message instead.
find_library(NETCDF_LIBRARY NAMES netcdf)              # :427
if(NOT NETCDF_LIBRARY)                                  # :428
  message(FATAL_ERROR                                   # :429   ← 是 FATAL_ERROR，不是 WARNING
    "netcdf not found, but test_io_multidim / test_io_roundtrip_matrix / "
    "test_io_subdataset_inventory / test_io_fabric_multidim_11 link it "
    "directly. Install the netcdf development package (libnetcdf-dev, "
    "netcdf, or vcpkg port) or set NETCDF_LIBRARY to a found library, "
    "then reconfigure.")
endif()
target_link_libraries(test_io_multidim PRIVATE ${NETCDF_LIBRARY})           # :436
target_link_libraries(test_io_roundtrip_matrix PRIVATE ${NETCDF_LIBRARY})   # :437
target_link_libraries(test_io_subdataset_inventory PRIVATE ${NETCDF_LIBRARY})        # :570
target_link_libraries(test_io_fabric_multidim_11 PRIVATE ${NETCDF_LIBRARY})          # :607
```

判定证据：`if(NOT NETCDF_LIBRARY)` 的分支体是 `message(FATAL_ERROR)`（:429），configure 在此即终止，因此 `NETCDF_LIBRARY-NOTFOUND` **永远不可能**到达 :436/:437/:570/:607 的 `target_link_libraries`。

> **对 BASELINE.md 的修正**: `.planning/r12-g-build-test-deploy/BASELINE.md:91` 记为
> “PARTIALLY_FIXED（有告警，仍会把 NOTFOUND 字面量喂给 linker）”——**该判定不成立**。
> 该行把 :428 的守卫当成 `message(WARNING)`，实际是 `FATAL_ERROR`。正确判定为 **ALREADY_FIXED**，
> 但引入了新的次级问题（见 G05-1 / G05-2）：`find_library` 只查库不查头（假成功），且把注释自称的
> “optional” 依赖变成了 configure 期硬必需。

### 1.2 #1468 · `tests/CMakeLists.txt:642` “WIN32 块重复 link `test_io_fabric_catalog`” → **NOT_REPRODUCIBLE**

- `tests/CMakeLists.txt:642` 当前实际内容是 `target_sources(test_io_microbench PRIVATE support/http_range_server.cpp)`，与 `test_io_fabric_catalog` 无关（行号已漂移）。
- 全文件对 `test_io_fabric_catalog` 的 `ws2_32` 链接只有 **1 处**：
  - `tests/CMakeLists.txt:615` `sicnu_add_io_test(test_io_fabric_catalog)`
  - `tests/CMakeLists.txt:621` `target_link_libraries(test_io_fabric_catalog PRIVATE ws2_32)`（在 :618 `if(WIN32)` 内）
- 全文件 `ws2_32` 链接共 15 处（:444/:507/:515/:523/:541/:549/:590/:597/:602/:613/:621/:629/:645/:10180），**每个 target 各 1 次，无重复**。

判定：**不可复现**，列入排除项（详见 §3）。

### 1.3 依赖探测“假成功”普查（required vs optional）

| 探测点 | 文件:行号 | required/optional | 缺失时行为 | 是否“假成功” |
|---|---|---|---|---|
| netcdf 库 | `tests/CMakeLists.txt:427` | 注释自称 optional（:423），实为 **required** | `FATAL_ERROR` 精准失败 | **是**（只查库，不查头 → 见 G05-1） |
| netcdf 头 `netcdf.h` | **不存在任何 `find_path`** | — | 无探测 | 3 个 TU `#include <netcdf.h>`：`tests/test_io_multidim.cpp:17`、`tests/test_io_subdataset_inventory.cpp:16`、`tests/test_io_fabric_multidim_11.cpp:21` |
| setupapi / version（WIN32） | `src/core/CMakeLists.txt:2754`/`:2755` | 无 REQUIRED、无守卫 | **NOTFOUND 字面量进 link line**（:2756） | 见 G05-3 |
| ONNX Runtime | `CMakeLists.txt:360`/`:364` | `if(SICNU_WITH_ONNX_RUNTIME)` + `if(INCLUDE AND LIBRARY)` 双守卫 | `message(STATUS "…compiled as a stub")`（:368） | 否，正确 |
| TensorRT | `src/operators/CMakeLists.txt:311-312` | `if(SICNU_ENABLE_TENSORRT)` + `if(INCLUDE AND LIBRARY)` | `message(STATUS "…SDK was not found")` | 否，正确 |
| OpenVINO | `src/operators/CMakeLists.txt:325-326` | 同上 | 同上 | 否，正确 |
| libm | `cmake/MuParserX/CMakeLists.txt:24-26`、`cmake/LibSVM/CMakeLists.txt:29-31` | `if(M_LIBRARY)` 守卫 | 静默不链接（正确，libm 通常合入 libc） | 否 |
| 特性编译探针 | `cmake/SicnuFeatureProbes.cmake:110-121` | DEPENDS 缺失 → `message(STATUS "SKIPPED")` + 宏置 0 | 显式 SKIP 且有计数 | 否，正确（是本仓库 optional 处理的范式） |

**结论**：把 NOTFOUND 变量直接塞进 `target_link_libraries` 的现存位置只有 **1 处** —— `src/core/CMakeLists.txt:2756`（G05-3）。netcdf 的同类问题已被 `FATAL_ERROR` 封死（1.1）。

`cmake/SicnuDepDoctor.cmake` 只被 `sicnu_require_dependency`（required，:177-233）与 `sicnu_require_optional_dependency`（:238-255）两条路径使用；调用点全部在 `CMakeLists.txt:115-168/:642` 与两个 fixture（`tests/fixtures/dep_doctor_fixture/CMakeLists.txt`、`tests/fixtures/sqlite3_probe/CMakeLists.txt`）。**netcdf 未走 doctor**。

---

## 2. 候选缺陷清单

### G05-1 · netcdf 只探测库、不探测头 → 依赖探测假成功（configure 绿 / 编译红）
- **严重度**: **P1**
- **根因一句话**: `find_library(NETCDF_LIBRARY NAMES netcdf)` 只验证库文件存在，从不验证 `#include <netcdf.h>` 能否解析，也没有给任何 target 加 netcdf 的 include 目录。
- **文件:行号**
  - 探测: `tests/CMakeLists.txt:427`（无 `find_path`，全文件 0 处 `find_path`）
  - 消费: `tests/CMakeLists.txt:436`、`:437`、`:570`、`:607`
  - 头文件需求: `tests/test_io_multidim.cpp:17`、`tests/test_io_subdataset_inventory.cpp:16`、`tests/test_io_fabric_multidim_11.cpp:21`
  - include 目录来源: `tests/CMakeLists.txt:158-161`（`sicnu_add_io_test` 只加 `${CMAKE_SOURCE_DIR}/src` 与 `${CMAKE_BINARY_DIR}`）；`tests/CMakeLists.txt:606`（`test_io_fabric_multidim_11` 额外只加 `${CMAKE_CURRENT_SOURCE_DIR}`）
- **最小 RED 复现命令**（纯文本，无需编译）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  grep -n "find_path\|NETCDF_INCLUDE_DIR" tests/CMakeLists.txt   # 预期: >=1 条 netcdf.h 探测；实际: 0 条
  grep -n "netcdf.h" tests/*.cpp                                  # 实际: 3 个 TU 直接 include
  ```
  判定：3 个 TU 直接 `#include <netcdf.h>`，而 CMake 侧对 netcdf 头的探测数为 0。
- **预期 vs 实际**
  - 预期：库与头**同时**被探测（或至少在探测成功后 `try_compile`/`check_include_file` 复核），头缺失时 configure 期就给出与 :429 同级的精准报错并点名缺 `netcdf.h`。
  - 实际：只要 `libnetcdf.so/.lib` 被找到，configure 判定“已找到”；当头不在编译器默认 include 路径（Windows vcpkg 前缀、conda 前缀、自定义 prefix）时，失败推迟到编译期，报 `fatal error: netcdf.h: No such file or directory`，且一次性炸 3 个 target。**这正是“找到了库但没找到头”的假成功。**
- **最小修复方向**（不实现）：把 `tests/CMakeLists.txt:427` 换成 `find_path(NETCDF_INCLUDE_DIR netcdf.h)` + `find_library(...)` 成对探测，并在 :436/:437/:570/:607 之外对 3 个含 `netcdf.h` 的 target 追加 `target_include_directories(... ${NETCDF_INCLUDE_DIR})`；或直接使用仓库里已存在的正确模块 `external/mdal/cmake/FindNetCDF.cmake:18,22,29`（`FIND_PATH` + `FIND_LIBRARY` + `FIND_PACKAGE_HANDLE_STANDARD_ARGS`）——该模块目前**未被项目任何地方使用**（全仓库 grep 零命中）。

### G05-2 · 自称 optional 的 netcdf 被 `FATAL_ERROR` 变成硬必需，且不在 vcpkg 清单 → 干净 Windows/离线 lane configure 直接死
- **严重度**: **P1**
- **根因一句话**: 注释与依赖摘要都声明 netcdf 是“可选、经 GDAL driver 提供”，实现却用 `FATAL_ERROR` 把它变成 configure 期硬必需；而仓库的 Windows 依赖清单 `vcpkg.json` 里根本没有 netcdf。
- **文件:行号**
  - `tests/CMakeLists.txt:423`（注释自称 “optional DRIVER dependency”）
  - `tests/CMakeLists.txt:428-435`（`FATAL_ERROR`）
  - `cmake/SicnuDepDoctor.cmake:283`（`message(STATUS "  NetCDF: provided through GDAL drivers …")`）
  - `vcpkg.json` `dependencies`（14 项：gdal/proj/geos/protobuf/libzip/expat/sqlite3/pcre2/gsl/curl/zstd/jsoncpp/opencv/…，**无 netcdf**）
- **最小 RED 复现命令**（纯文本，无需编译）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  python -c "import json;print(sorted(d['name'] for d in json.load(open('vcpkg.json'))['dependencies']))"
  # 实际输出不含 'netcdf'
  grep -n "FATAL_ERROR" -B3 tests/CMakeLists.txt | sed -n '1,12p'   # :428-435
  ```
- **预期 vs 实际**
  - 预期：optional 依赖缺失 → configure 期 **明确 SKIP + 计数**（范式见 `cmake/SicnuFeatureProbes.cmake:117-120`），把 4 个 netcdf 相关 suite 标成未注册并计入 REPORT，其余 ~970 个 target 照常构建。
  - 实际：一台按仓库自带 `vcpkg.json` 装依赖的干净 Windows 机器上，`find_library` 必然失败 → 整个 configure 在第 :428 行中止，**一个测试都配不出来**。这把 #1468 的“链接期 cryptic fail”换成了“configure 期全量阻塞”，且与 :423/:283 的自述直接矛盾。
- **最小修复方向**（不实现）：把 :428 的 `FATAL_ERROR` 改为 `message(STATUS/WARNING …)` + 跳过 `sicnu_add_io_test(test_io_multidim)` / `test_io_roundtrip_matrix` / `test_io_subdataset_inventory` / `test_io_fabric_multidim_11` 四行注册，并累加一个 `SICNU_SKIPPED_SUITES` 计数在 configure 末尾打印（同时满足 G04-1 的“静默门计数”需求）；或反向：把 `netcdf-c` 加进 `vcpkg.json` 并承认它是 required。二者选一，但不能像现在这样“注释说 optional、代码说 required、清单里没有”。

### G05-3 · WIN32 未加保护的 `SETUPAPI_LIBRARY` / `VERSION_LIBRARY` 会把 NOTFOUND 字面量喂给 linker（#1468 同类缺陷的新实例）
- **严重度**: **P1**（与 #1468 同一缺陷类，且落在本次 R12-G 的 Windows 离线 lane 上）
- **根因一句话**: 两条 `find_library` 没有 `REQUIRED`、没有 `if(SETUPAPI_LIBRARY)` 守卫，结果变量被直接展开进 `target_link_libraries`。
- **文件:行号**
  ```cmake
  # src/core/CMakeLists.txt:2753-2757
  if (WIN32)                                                                       # :2753
    find_library(SETUPAPI_LIBRARY NAMES setupapi PATHS $ENV{LIB})                  # :2754
    find_library(VERSION_LIBRARY NAMES version PATHS $ENV{LIB})                    # :2755
    target_link_libraries(qgis_core wsock32 ${SETUPAPI_LIBRARY} dbghelp ${VERSION_LIBRARY})  # :2756
  endif()                                                                          # :2757
  ```
- **最小 RED 复现命令**
  1) 静态确证（无需编译，已执行）：
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  grep -n -B2 -A1 "SETUPAPI_LIBRARY" src/core/CMakeLists.txt
  # 实际: 2754 find_library / 2755 find_library / 2756 target_link_libraries —— 中间无 if() 守卫
  ```
  2) 行为复现（需要一次性 scratch 目录，**不写入仓库**；本审计未执行，因只读约束）：
  ```bash
  mkdir -p /tmp/g05probe && cd /tmp/g05probe
  printf 'find_library(SETUPAPI_LIBRARY NAMES setupapi PATHS "$ENV{LIB}")\nmessage(STATUS "value=[${SETUPAPI_LIBRARY}]")\n' > probe.cmake
  cmake -P probe.cmake
  ```
- **预期 vs 实际**
  - 预期：缺失时 configure 期精准失败并给出安装/前缀指引，或不链接并打印 STATUS。
  - 实际：`PATHS $ENV{LIB}` 依赖 `LIB` 环境变量——该变量**只在 VS 开发者命令行里存在**；在非 dev-shell（含本仓库 BASELINE §4 记录的“cmd.exe 被拦截、改用 PowerShell `Enter-VsDevShell`”变通路径之外的任何 shell）下 `LIB` 为空，而 CMake 在 Windows 上不会自动补 Windows SDK 的 lib 目录，`find_library` 返回 `SETUPAPI_LIBRARY-NOTFOUND` / `VERSION_LIBRARY-NOTFOUND`，这两个字面量被原样写进 `qgis_core` 的链接行 → **链接期 cryptic fail，且挂在 qgis_core 上，下游数百个测试 target 连锁失败**（与 #1468 的失败形状完全一致）。
- **最小修复方向**（不实现）：在 :2754/:2755 后加 `if(NOT SETUPAPI_LIBRARY OR NOT VERSION_LIBRARY)` → `message(FATAL_ERROR …)`（与 netcdf :428 同款），或改用 `find_library(... REQUIRED)`；并把该类“未守卫的 find_* 结果进 target_link_libraries”加入静态门禁（现有的 `check_platform_link_guards` 只认 `ws2_32`/`wsock32`：`tests/test_build_wiring_drift.cpp:2041-2065`，覆盖不到）。

### G04-1 · 41 个条件块静默吞掉整批测试，零 message / 零 SKIP 计数
- **严重度**: **P1**
- **根因一句话**: `tests/CMakeLists.txt` 用大量裸 `if(...)` 包裹测试注册，条件为假时整批 target 根本不会被创建，且块内既无 `message()` 也无 `else()` 分支，CTest 侧也没有任何 SKIP/REPORT 计数能对账。
- **文件:行号**（按被吞 target 数降序，全部实测自 if/endif 配对 census，块内 `message(` 计数 = 0）

  | 条件 | 行范围 | 被门控 target 数 | 块内 message/else |
  |---|---|---|---|
  | `if(TARGET sicnu_operators)` | `tests/CMakeLists.txt:6527-7273` | **45** | 0 / 无 |
  | `if(TARGET qgis_app_classify)` | `tests/CMakeLists.txt:5511-6266` | **39** | 0 / 无 |
  | `if(SICNU_HAS_OPENCV AND OpenCV_FOUND)` | `tests/CMakeLists.txt:2969-3236` | **13** | 0 / 无 |
  | `if(NOT WIN32)` | `tests/CMakeLists.txt:10165-10226` | **7** | 0 / 无 |
  | `if(SICNU_HAS_OPENCV AND TARGET qgis_app_obia)` | `tests/CMakeLists.txt:6374-6522` | **6** | 0 / 无 |
  | `if(SICNU_EMBED_PYTHON)` | `tests/CMakeLists.txt:7824-8055` | **6** | 0 / 无 |
  - 45-target 块代表：`test_pipeline_runner`、`test_rs_operator`、`test_temporal_*`（16 个）、`test_spectral_*`（13 个）、`test_sar_*`、`test_mnf`、`test_opencv_operators` …
  - 39-target 块代表：`test_classification_*`（6）、`test_classifier_*`（8）、`test_layout_*`、`test_pixel_*`、`test_roi_tool_*`、`test_help_system` …
  - 7-target WIN32 块：`test_cli_batch_manifest`、`test_cli_commands_json`、`test_cli_error_messages_r4`、`test_cli_exit_codes_r4`、`test_cli_fail_fast_r4`、`test_cli_help_vocabulary_r4`、`test_surface_parity_r4`
- **可达性佐证（决定严重度上限）**
  - `sicnu_operators`：`CMakeLists.txt:863` `add_subdirectory(src/operators)` 无条件 + `src/operators/CMakeLists.txt:260` `add_library(sicnu_operators SHARED)` 无条件 → 45-target 门**实际恒真**，属“模式坏但当前不触发”。
  - `qgis_app_classify`：`src/app/classification/CMakeLists.txt:8-9` `if(NOT SICNU_HAS_OPENCV) … message(STATUS "Skipping qgis_app_classify …")` → 库侧**有** STATUS，但测试侧 :5511 无；且 `SICNU_HAS_OPENCV` 由 `CMakeLists.txt:186-197` 的 `find_package(OpenCV QUIET …)` 结果决定（OpenCV 缺 `ml` 组件即为 FALSE）→ **39 个 target 在 OpenCV 不全的机器上静默消失**，只有库侧一行 STATUS，没有任何计数。
  - `SICNU_EMBED_PYTHON`：`CMakeLists.txt:288` `option(... OFF)` → **默认 OFF**，6 个 Python 测试默认静默缺席。
  - `NOT WIN32`：**本 worktree 正是 Windows**，7 个 CLI/surface parity 测试在本机 lane 上静默缺席。
- **最小 RED 复现命令**（纯文本 census，无需编译，**已执行**）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test/tests
  python - <<'PY'
  import re
  t=open('CMakeLists.txt',encoding='utf-8',errors='replace').read()
  def sc(l):
      o=[];q=False
      for c in l:
          if c=='"': q=not q
          if c=='#' and not q: break
          o.append(c)
      return ''.join(o)
  S=[sc(x) for x in t.split('\n')]
  st=[];blocks=[]
  for i,l in enumerate(S,1):
      m=re.match(r'\s*(if|while|foreach|function|macro|endif|endwhile|endforeach|endfunction|endmacro)\b',l)
      if not m: continue
      k=m.group(1)
      if k in ('if','while','foreach','function','macro'): st.append((k,i,l.strip()))
      elif st:
          k0,s,c=st.pop(); blocks.append((k0,s,i,c))
  nm=re.compile(r'\b(?:sicnu_add_[a-z_]*test|sicnu_discover_tests|catch_discover_tests|add_test)\s*\(\s*(?:NAME\s+)?([A-Za-z0-9_]+)')
  mg=re.compile(r'\bmessage\s*\(')
  rows=[]
  for k0,s,e,c in blocks:
      if k0!='if': continue
      b='\n'.join(S[s:e-1]); n=sorted(set(nm.findall(b)))
      if n: rows.append((len(n),s,e,c.strip(),len(mg.findall(b))))
  rows.sort(reverse=True)
  for n,s,e,c,m in rows[:6]: print(n,"lines",s,"-",e,"msg=",m,"|",c[:70])
  PY
  ```
  实际输出（节选）：
  ```
  45 lines 6527 - 7273 msg= 0 | if(TARGET sicnu_operators)
  39 lines 5511 - 6266 msg= 0 | if(TARGET qgis_app_classify)
  13 lines 2969 - 3236 msg= 0 | if(SICNU_HAS_OPENCV AND OpenCV_FOUND)
  7  lines 10165 - 10226 msg= 0 | if(NOT WIN32)
  6  lines 6374 - 6522 msg= 0 | if(SICNU_HAS_OPENCV AND TARGET qgis_app_obia)
  6  lines 7824 - 8055 msg= 0 | if(SICNU_EMBED_PYTHON)
  ```
  （共 41 个含测试注册的 `if` 块，其中 39 个 `message` 计数为 0）
- **预期 vs 实际**
  - 预期：每个门控批在条件为假时打印一行 `STATUS/SKIP` 并累加计数（如 `SICNU_SKIPPED_SUITES`，configure 末尾汇总），使“本机比全量少 N 个 suite”可被对账。
  - 实际：条件为假时这批 target 从未被创建，CTest 完全不知道它们存在；`ctest` 返回全绿，与“全量构建”的结果无法区分。
- **最小修复方向**（不实现）：在 `sicnu_add_test` / `sicnu_add_io_test` / `sicnu_discover_tests` 之外引入一个 `sicnu_skip_suite(<name> <reason>)` 宏（`tests/CMakeLists.txt:41`/`:102`/`:152` 附近），在每个条件块的 `else()` 分支调用它并 `list(APPEND SICNU_SKIPPED_SUITES …)`，在 `tests/CMakeLists.txt` 末尾 `message(STATUS "… skipped N suites: …")`；至少优先补 6 个大块。

### G04-2 · 唯一真正“有源文件但没注册”的 suite：`test_challenger_m1_2_harness.cpp`
- **严重度**: **P2**（是手工压测 harness，不是 Catch2 suite，故非“丢失的测试”，但会污染任何纯文本 census）
- **根因一句话**: 该 TU 以 `test_` 开头、位于 `tests/` 根，但既未被 `tests/CMakeLists.txt` 引用，也不是 Catch2 suite（自带 `int main()`、无 `TEST_CASE`），全仓库零引用。
- **文件:行号**
  - `tests/test_challenger_m1_2_harness.cpp:717` `int main()`；全文件无 `TEST_CASE`
  - `tests/test_challenger_m1_2_harness.cpp:1` 自述 “Empirical Challenger Stress Harness for Milestone 1 Memory Governance & Leak Detection”
  - 引用普查：`grep -rn "challenger_m1_2|m1_2_harness"` 全仓库仅命中该文件自身（`tests/test_challenger_m1_2_harness.cpp:1`）
- **最小 RED 复现命令**（纯文本，**已执行**）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  grep -rn "challenger_m1_2\|m1_2_harness" --include=*.cpp --include=*.h --include=*.txt --include=*.cmake --include=*.md .
  # 实际: 仅 tests/test_challenger_m1_2_harness.cpp:1 自引用
  grep -c "TEST_CASE" tests/test_challenger_m1_2_harness.cpp   # 实际: 0
  ```
- **预期 vs 实际**
  - 预期：`tests/` 下每个 `test_*.cpp` 要么被注册进 CTest，要么被显式标注为“手工 harness”并移出 `test_` 命名/移入 `tests/manual/`。
  - 实际：悬空存在，任何基于“`tests/test_*.cpp` 必须被注册”的门禁（含 `tests/test_build_wiring_drift.cpp:338-359` 规则 3）都会把它判为未注册违规。
- **最小修复方向**（不实现）：重命名为 `benchmark_challenger_m1_2_harness.cpp`（或移入 `tests/manual/`）并在 `docs`/`TEST_INFRA.md` 标注为手工执行；或补一条 `add_executable` + `add_test` 并标 `LABELS LONG_RUN`。

### G04-3 · 现有漂移检测全部依赖已构建产物；纯文本 census 对 `foreach` 组合名系统性误报
- **严重度**: **P2**
- **根因一句话**: 仓库唯一的 CTest 漂移门禁要求 build 树里的 `*_tests.cmake`；唯一的纯文本规则用“stem 整词出现”判定注册，对 `foreach(${VAR})` 拼出的目标名必然误报。
- **文件:行号**
  - `.planning/verify-chain-r4/chain_gate_census.sh:86`（`find "$BUILD_DIR" -maxdepth 2 -name '*_tests.cmake'`）、`:107`（BUILD-GAP 同样查 build 树）、`:144`（`ctest --test-dir "$BUILD_DIR" -N -R "^r4::"`）→ **无 build 目录则 §1/§2/§4/§5 全部失效**；`:30-35` 自己承认 “foreach 注册且未进入当前构建人群” 是残余盲区。
  - `.planning/verify-chain-r4/chain_gate_census.sh:42` `DOMAIN_RE='verif|grader|preflight|suitab|science|evidence'` → 只覆盖 6 个关键字，G04-1 里 45/39/7/6 个被吞 target **绝大多数不在其中**。
  - `tests/test_build_wiring_drift.cpp:338-359` 规则 3：`if (!contains_word(tests_cm, stem)) unregistered.push_back(stem)`（`contains_word` 见 `:94-108`）。
- **误报实测量**（按规则 3 的注释剥离 + 词边界算法忠实复现，**已执行**）：
  ```
  tests\test_challenger_m1_2_harness.cpp        ← 真阳性（= G04-2）
  tests\test_verifier_adversarial_14.cpp         ← 假阳性（已注册）
  tests\test_verifier_checks_14.cpp              ← 假阳性（已注册）
  tests\test_verifier_core_14.cpp                ← 假阳性（已注册）
  tests\test_verifier_packs_14.cpp               ← 假阳性（已注册）
  tests\test_verifier_provenance_14.cpp          ← 假阳性（已注册）
  tests\test_verifier_render_14.cpp              ← 假阳性（已注册）
  ```
  6 个 `_14` suite 的真实注册点（`foreach` 组合名，纯文本不可见）：
  ```cmake
  # tests/CMakeLists.txt:15594-15621
  set( SICNU_VERIFIER_LANES core checks provenance packs render drift adversarial )   # :15594-15602
  foreach( LANE ${SICNU_VERIFIER_LANES} )                                             # :15604
      set( LANE_TARGET "test_verifier_${LANE}_14" )                                   # :15605
      add_executable( ${LANE_TARGET} ${LANE_TARGET}.cpp )                             # :15606
      …
      sicnu_discover_tests( ${LANE_TARGET} TEST_PREFIX "${LANE_TARGET}::" )           # :15620
  endforeach()                                                                        # :15621
  ```
- **已有可脱离构建的基础设施（正面事实）**：`scripts/check_presets.py` 是纯 JSON 静态门禁，实测 **ALL PASS / exit 0**（30 项断言，无需 configure）；`scripts/verification_ladder.py --list` 实测 **exit 0**（无需 build 树）；`scripts/dev/narrow_targets.py`（706 行）已是纯文本 CMake wiring 解析器（`TARGET_CMDS` 见 `:39`），可复用其解析器扩展出“注册 census”。
- **预期 vs 实际**
  - 预期：存在一条 `python scripts/xxx.py`（或 `cmake -P`）能在**未 configure 的 worktree** 上给出“应注册 target 全集 / 条件门控清单 / foreach 组合名展开”，并与上一次 configure 的实际清单对账。
  - 实际：没有。`chain_gate_census.sh` 全条依赖 build 树；`test_build_wiring_drift` 规则 3 依赖编译该 3480 行 TU（本身就是重型编译）；纯文本 census 对 `foreach` 误报。
- **最小修复方向**（不实现）：在 `scripts/dev/` 下新增纯文本 census（复用 `narrow_targets.py` 的 CMake 解析），把 `set(<LIST> …)` + `foreach(<V> ${<LIST>})` + `set(<T> "prefix_${V}_suffix")` 模板**展开**后再做整词匹配，并输出 `(a)` 无条件应注册全集、`(b)` 每个条件门控的 target 数与条件文本、`(c)` 未注册 orphan 三类，配 `--json`，挂到 `check_presets.py` 同级的离线门禁。

### G04-4 · 容错发现把一个二进制的全部用例压成 1 个失败项，无 SKIP 计数
- **严重度**: **P2**
- **根因一句话**: `catch_discover_tests_impl` 探测失败时 `file(WRITE …)` **覆盖**该二进制的 ctest 文件，只留一条 `<exe>_DISCOVERY_FAILED`，原二进制的全部 Catch2 用例从 census 中蒸发。
- **文件:行号**: `cmake/SicnuCatchAddTests.cmake:72-108`（判定 `:72`、占位名 `:74`、`WARNING` `:75`、`file(WRITE)` `:106`、`return()` `:107`）
- **最小 RED 复现命令**（纯文本 + 逻辑推演，无需编译）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  grep -n "DISCOVERY_FAILED\|file(WRITE" cmake/SicnuCatchAddTests.cmake
  # 实际: :74 占位名构造 / :97 add_test(占位) / :106 file(WRITE "${_ctest_file}" …)
  ```
- **预期 vs 实际**
  - 预期：一个二进制发现失败时，除了 1 个失败项，还应登记“该 target 的 N 个用例未进入 census”，并在汇总中体现（否则 `ctest` 总数下降无人察觉）。
  - 实际：占位脚本只有 1 条 `add_test(<exe>_DISCOVERY_FAILED <exe> --list-tests)`（`:97`），`ctest` 总数相对健康构建少掉该二进制的全部用例，但**没有任何计数/报表字段**记录这个缺口；`chain_gate_census.sh` 的 BUILD-GAP 只在 DOMAIN_RE 6 关键字命中时才可能察觉。
- **最小修复方向**（不实现）：占位脚本里额外写入一个 `set_tests_properties(... LABELS DISCOVERY_FAILED)`，并在 configure 侧累加 `SICNU_DISCOVERY_FAILED_TARGETS`，末尾 `message(STATUS …)` 汇总（与 G04-1 的 SKIP 计数合并为同一张表）。

### G12-1 · 全部条目 `not-built` 时 ladder 仍 `OVERALL: PASSED` 且 exit 0
- **严重度**: **P2**（反作弊红线的**边界**项：item 层诚实，aggregate 层误导）
- **根因一句话**: `lane_ok` 只在 `failed/timeout` 时置假；`skipped/not-built` 仅在 `--strict` 下才计失败，而 `--strict` 非默认。
- **文件:行号**
  - `scripts/verification_ladder.py:533-536`
    ```python
    if item["status"] in ("failed", "timeout"):
        lane_ok = False
    elif item["status"] in ("skipped", "not-built") and args.strict:
        lane_ok = False
    ```
  - `scripts/verification_ladder.py:551` `lane_result["status"] = "passed" if lane_ok else "attention"`
  - `scripts/verification_ladder.py:562` `results["overall"] = "passed" if overall_ok else "attention"`
  - `scripts/verification_ladder.py:573` `return 0 if overall_ok else 1`
  - `not-built` 的产生点：`:513-515`（`find_binary` 返回 None）
- **最小 RED 复现命令**（需要一个已 configure 但未编译的 build 树；**本审计未执行**——本 worktree 无 build 目录，且禁止重型编译）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  python scripts/verification_ladder.py --build-dir <configured-but-unbuilt-tree> \
      --lanes L1 --json %TEMP%/ladder-L1.json ; echo "exit=$?"
  ```
- **预期 vs 实际**
  - 预期：一个条目都没真正执行时，overall 不应是 `passed`，exit 不应是 0（或至少 `overall` 应为 `insufficient-evidence` 之类）。
  - 实际：4 个 L1 条目全部 `find_binary` 失败 → 全部 `not-built` → `lane_ok=True` → `L1 status=passed` → `overall=passed` → 打印 `OVERALL: PASSED` 并 `return 0`。即“**空跑也能全绿**”。
- **缓解证据（降级为 P2 的理由）**：
  - item 层确实没有把 not-built 记成 passed（状态字段独立），docstring `:28` 也明写 “`not-built` and `skipped` are NEVER reported as pass”；
  - 这是**有记录的 deliberate 设计**：`.planning/verification-platform-8/REVIEW_LOG.md:63` “| B9 | --strict was a no-op | P3 | FIXED: default exit fails on failed/timeout; --strict also fails skipped/not-built”；
  - 下游 `scripts/collect_readiness.py:207-214` 会把非 passed 计数折算成 `insufficient-evidence`，不产生假 readiness。
- **最小修复方向**（不实现）：`overall` 增加第四态（如 `"passed-with-gaps"` / `"insufficient-evidence"`），或默认要求“至少 1 个 item 真正 executed”，把 `--strict` 的语义收窄为“连 skipped 也不许有”。

### G12-2 · L8 的 `ctest` 通道未加 `--no-tests=error`，空注册会被判 passed
- **严重度**: **P2**
- **根因一句话**: `run_ctest` 只用 `returncode == 0` 判成功，而 CMake 在“没有匹配到任何测试”时默认 `ignore`（exit 0）。
- **文件:行号**
  - `scripts/verification_ladder.py:347-361`（`:350-352` 拼命令、`:359-360` `if proc.returncode == 0: return "passed"`）
  - `scripts/verification_ladder.py:193` L8 item `("ctest_all", ".*", "ctest", 14400)`
  - 选项可用性实测：`C:/Qt/Tools/CMake_64/bin/ctest.exe --help` 含 `--no-tests=<[error|ignore]>`（CMake 3.29.3）
- **最小 RED 复现命令**（需一次性 scratch 目录；**本审计未执行**，只读约束）
  ```bash
  mkdir -p /tmp/g12ctest && cd /tmp/g12ctest
  : > CTestTestfile.cmake
  ctest --test-dir . -R ".*" ; echo "exit=$?"                 # 默认: No tests were found!!! + exit 0
  ctest --test-dir . -R ".*" --no-tests=error ; echo "exit=$?" # 预期非 0
  ```
- **预期 vs 实际**
  - 预期：L8 若一个用例都没跑（例如 G04-1 的条件门全部为假、或 CTest 注册漂移），应判 `failed`。
  - 实际：`ctest -R ".*"` 空匹配 → exit 0 → L8 `passed`。这是 G04-1“静默门”与 G12 的**交叉放大点**：条件门吞掉的测试，L8 也发现不了。
- **最小修复方向**（不实现）：`run_ctest` 命令追加 `--no-tests=error`（CMake ≥3.18），或在返回前用 `ctest -N` 计数断言 > 0（与 `chain_gate_census.sh:144` 用 `grep -c 'Test *#'` 断言 ≥1 的既有做法一致）。

### G12-3 · `--resume` 对 `kind == "target"`（L0）不做身份校验，旧 pass 被永久复用
- **严重度**: **P3**
- **根因一句话**: resume 的 staleness 判定只在 `kind in ("test","bench")` 分支里做，`target` 类条目（目前只有 L0 `sicnu_header_probes`）只要有历史 `passed` 就无条件复用。
- **文件:行号**: `scripts/verification_ladder.py:468-487`（`:473` `binary = find_binary(...) if kind in ("test","bench") else None`、`:475` `if kind in ("test","bench"):`）；身份写入同样只覆盖 test/bench：`:539-547`
- **最小 RED 复现命令**（静态确证，**已执行**）
  ```bash
  cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
  grep -n 'if kind in ("test", "bench")' scripts/verification_ladder.py   # 实际: :475 与 :540 两处
  grep -n '"header_probes", "sicnu_header_probes", "target"' scripts/verification_ladder.py  # :67
  ```
- **预期 vs 实际**
  - 预期：L0 的 header 自包含探针在源码变化后重建并被重跑。
  - 实际：`kind=="target"` → `prior_stale` 恒 False（`:474`）→ 只要状态文件里有一条 `L0:header_probes = passed`，后续 `--resume` 永远打印 `[resume-pass]` 并跳过，即使 `src/**/*.h` 已大改。
- **最小修复方向**（不实现）：给 `target` 类条目构造身份（例如该 target 依赖的源文件 mtime 聚合，或直接用 `current_sha`），或干脆禁止 `target` 类条目 resume。

---

## 3. 明确排除项（看着可疑但没问题）

1. **`tests/CMakeLists.txt:642` “WIN32 块重复 link `test_io_fabric_catalog`”（BASELINE.md:93）** —— 不可复现。当前 :642 是 `test_io_microbench` 的 `target_sources`；`test_io_fabric_catalog` 的 `ws2_32` 链接仅 :621 一处；全文件 15 处 `ws2_32` 链接按 target 去重后无重复。支持源（`support/http_range_server.cpp`）被多个 target 共享属正常 `target_sources` 复用，不是重复 link。
2. **5 个 `test_verifier_*_14.cpp` 是“未注册孤儿”** —— 假阳性。真实注册在 `tests/CMakeLists.txt:15604-15621` 的 `foreach` 循环里（`add_executable(${LANE_TARGET} …)` :15606，`sicnu_discover_tests(… TEST_PREFIX "${LANE_TARGET}::")` :15620）。纯文本 stem 匹配看不见组合名（详见 G04-3）。
3. **重复注册 / 同名 test 覆盖** —— 未发现。对 `(?:sicnu_discover_tests|catch_discover_tests)\(<target>` 做全文件计数，**0 个 target 被调用 >1 次**；`add_executable` 目标 537 个，除 14 个明确非测试的 helper/benchmark/tool（见下条）外全部有注册调用。
4. **14 个有 `add_executable` 但无测试注册的目标** —— 全部合理：`benchmark_io`、`benchmark_temporal10`、`benchmark_contract9`、`benchmark_quality7`、`benchmark_scale8`（benchmark）、`sar_fake_unwrap_provider`、`surface_snapshot_gen`、`surface_mcp_host`、`workflow_crash_helper`、`exprs_ep_helper`、`lab_grading_fixture_gen`、`capability_knowledge_tool`、`test_teaching_fake_grader_cli`、`sicnu_fake_python_worker`（helper/fixture/CLI）。注意 `chain_gate_census.sh:53-56` 已为 `test_teaching_fake_grader_cli` 显式 allowlist。
5. **netcdf NOTFOUND 进 `target_link_libraries`（#1468 原始断言）** —— 已被 `FATAL_ERROR` 封死，NOTFOUND 不可能到达 :436/:437/:570/:607（详见 §1.1）。
6. **`cmake/SicnuFeatureProbes.cmake:120` 在 macro 里用 `return()`** —— 潜在隐患但**当前安全**：`return()` 在 macro 中会从“外层作用域”返回，若该 macro 在 `function()` 内被调用会吞掉函数剩余逻辑。全仓库 5 个调用点（`CMakeLists.txt:700/707/714/718/730`）都在**目录作用域**，不在任何 function 内。记录备查，不建议本轮改动。
7. **`cmake/SicnuDepDoctor.cmake:53` `_SICNU_DEP_HINT_NetCDF` 是死代码** —— netcdf 从未走 `sicnu_require_dependency`，该提示行永不生效。属 P3 一致性问题（不是缺陷），已在 G05-1 的修复方向里一并提及。
8. **`cmake/SicnuTestEnv.cmake:99-109` Qt 插件路径探测在 Windows 上可能为空** —— 不算缺陷：`Qt6::qmake` imported target 在 `CMAKE_PREFIX_PATH` 指到 Qt6 时必然存在（`SicnuTestEnv.cmake:84-86`），`find_program` 回退（`:88-89`）只在异常配置下才走到；且即使为空也只影响 `QT_PLUGIN_PATH`，失败会显现在运行时而非静默通过。
9. **`verification_ladder.py` 把“没跑”记成 PASS（P0 反作弊红线）** —— **不成立**。item 层状态机严格区分 `passed/failed/not-built/timeout/skipped`（`:27-28`、`:533-536`）；exit-77 + `sicnu-skip:` sentinel 走独立 `skipped` 通道（`:316-317`、`:336-342`）；`collect_readiness.py:209-214` 的 `ready` 要求**全部** passed，否则降级 `insufficient-evidence`/`attention`。残留风险是 aggregate 层措辞（已单列为 G12-1，P2）。
10. **`P0→P1→P2→P3` 分层命名** —— 本仓库不存在该命名。实际分层是 `L0..L8` + 可选 `LS`（sanitizer）（`scripts/verification_ladder.py:63-197`，`LANE_ORDER` :197）。`--list` 实测 exit 0 且无需 build 树；默认 lanes `L0,L1,L2,L3,L4,L5,L7`（`:377`）排除 `LS/L6/L8`。JSON schema `exp.verification.ladder.v1`（`:53`），`--json` 输出可用；`collect_readiness.py` 同时产出 Markdown + JSON（`:231-277`），`docs/verification/READINESS.{md,json}` 已在仓库中存在。

---

## 4. 实测命令台账（可复现）

| # | 命令 | 结果 | 是否需编译 |
|---|---|---|---|
| 1 | `git status --short` / `git branch --show-current` / `git log --oneline -5` | 干净；`hardening/r12-g-build-test-offline`；HEAD `28abef358` | 否 |
| 2 | `python scripts/check_presets.py` | `check_presets: ALL PASS`（30 项），exit 0 | 否 |
| 3 | `python scripts/verification_ladder.py --list` | 列出 L0..L8 全部 lane/item，exit 0 | 否 |
| 4 | tests/`CMakeLists.txt` if/endif 配对 + 注册 census（inline python） | 41 个含注册的 `if` 块，39 个 block 内 `message`=0；最大 45/39/13/7/6/6 | 否 |
| 5 | `add_executable` vs 注册 census（inline python） | 537 targets / 973 注册名 / 14 个未注册（全 helper-benchmark）/ 0 个重复 discover | 否 |
| 6 | ladder item 名 vs CMake target 交叉核对（inline python） | 41→1 个不匹配，唯一为 `sicnu_header_probes`，实为 `tests/CMakeLists.txt:11227` 的 `add_custom_target` → 全匹配 | 否 |
| 7 | 规则 3 忠实模拟（注释剥离 + 词边界，inline python） | 7 项：6 个 `test_verifier_*_14` 假阳性 + 1 个 `test_challenger_m1_2_harness` 真阳性 | 否 |
| 8 | `grep -rn "challenger_m1_2\|m1_2_harness"` 全仓库 | 仅自引用 | 否 |
| 9 | `python -c "…vcpkg.json…"` | dependencies 14 项，无 netcdf | 否 |
| 10 | `grep -n "find_path\|NETCDF_INCLUDE_DIR" tests/CMakeLists.txt` | 0 命中 | 否 |
| 11 | `C:/Qt/Tools/CMake_64/bin/ctest.exe --help \| grep no-tests` | `--no-tests=<[error\|ignore]>` 存在（cmake 3.29.3） | 否 |

**未执行（受只读/无 build 树约束，结论由静态推导并在条目内标注）**：G05-3 的 `cmake -P` probe、G12-1 的 ladder 空跑、G12-2 的 `ctest --no-tests` 对比 —— 三者均需一次性 scratch 目录（位于仓库外）。
