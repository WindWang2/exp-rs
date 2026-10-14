# R12-G PERFORMANCE — 性能与资源证据

- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test` · 分支 `hardening/r12-g-build-test-offline`
- **目标**：只记录**真实测到**的数字。没测的一律 `NOT_RUN` / `NOT_MEASURED`，不补估算值。

---

## 1. 宿主环境基线（实测）

| 项 | 值 | 来源 |
|---|---|---|
| 逻辑处理器 | **16** | `os.cpu_count()` = 16；`$NUMBER_OF_PROCESSORS` = 16 |
| 系统盘 | `C:` 总 1.9 T / 已用 1.5 T / **可用 430 G**（77%） | `df -h /c` |
| `build-dev` 体积（已 configure、未编译） | **3.4 G** | `du -sh build-dev` |
| 物理内存 / 峰值 RSS | ⚠️ **NOT_MEASURED** | 本机无 `psutil`；本轮未做重型编译/测试 |

**工具链**（BASELINE.md §4 实测）：MSVC `19.38.33130`、CMake `C:/Qt/Tools/CMake_64/bin/cmake.exe` (3.29)、Ninja `C:/Qt/Tools/Ninja/ninja.exe`、Qt6 `C:/deps/Qt/6.8.0/msvc2022_64`、vcpkg 复用 `exp-rs/build-dev/vcpkg_installed`。

---

## 2. 构建耗时证据

### 2.1 ✅ 真实 configure 记录（`build-logs/r12g-configure5.log`）

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

### 2.2 ⚠️ 关键时间线缺口（如实记录）

| 事实 | 证据 |
|---|---|
| `build-dev/CMakeCache.txt` mtime | `Oct 14 01:25` |
| `tests/CMakeLists.txt` 本轮改动（新增 `find_path`） | 晚于 01:25 |
| `grep NETCDF_INCLUDE_DIR build-dev/CMakeCache.txt` | **无命中** |

→ **397.5 s 是 G05-1 改动「之前」的 configure 证据**；G05-1 的 GREEN2（重跑 configure）= ❌ **NOT_RUN**，并连带阻塞 L3 / L4。

**未重跑的原因**：① G05-2 未收敛（`:423` 注释自称 optional、`:428` 实现是 `FATAL_ERROR`，而 `vcpkg.json` 无 netcdf），先重配会把"可选依赖缺失"变成硬阻塞；② 单次 ≈ 501 s，须与并发 agent 的构建租约排队。

### 2.3 早期 configure 失败台账（环境引导，非代码缺陷）

| 日志 | 退出码 | 失败原因 |
|---|---|---|
| `r12g-configure.log` | 1 | vcpkg manifest 联网引导失败（`CreateFileW stdin failed with 231`）+ 连锁 `CMAKE_MAKE_PROGRAM not set` |
| `r12g-configure2.log` | 1 | `vcpkg manifest mode was disabled for a build directory where it was initially enabled` |
| `r12g-configure3.log` | 1 | MSVC 试编译失败：`rc.exe` 找不到 |
| `r12g-configure4.log` | 1 | `rc.exe` / `mt.exe` 不在 PATH（缺 Windows SDK bin） |
| `r12g-configure5.log` | **0** | 成功（397.5 s） |

→ 证明干净 Windows lane 的阻塞点**全在环境引导**，不在仓库代码。

---

## 3. `-j2` 上限依据

**策略出处**：`docs/development/resource-discipline.md`（ADR 0147 / D7）；`scripts/build.sh` 头部自述同一策略。

| 量 | 界 |
|---|---|
| 编译器/构建并行度 | 默认 **1**，硬上限 **2**；`-j3+` **拒绝**（exit 2），绝不静默执行 |
| 继承的 `CMAKE_BUILD_PARALLEL_LEVEL` / `CTEST_PARALLEL_LEVEL` 超上限 | **钳制**到上限（陈旧环境不得击败策略） |
| ctest 并行度 | 恒为 **1** |
| GUI 测试 | 恒为 `QT_QPA_PLATFORM=offscreen` |

**"为什么存在这些上限"**（原文直引）：
> Deep Qt/QGIS/GDAL translation units exhaust memory well before the CPU is
> saturated: the highest-value setting on a workstation is *serial compiles with
> early failure*, not throughput. Compile one target, read its errors, then move on.

**与本机 16 核的关系**：16 核不意味着可跑 `-j8`——瓶颈是**内存**而非 CPU。这也是 G06 存在的理由：`-j2` 只约束**单个**构建，同机多个 agent/worktree 各自守 2 核，机器承受的是**总和**，重链接阶段 OOM 会连坐其他 lane。

**实现位置**：`scripts/build.sh:35-43`（`JOBS_CAP` 校验）、`:57-90`（`resolve_jobs()`）、`:167-199`（守卫前缀）、`:275-303`（selftest 第 4 组）；`CMakePresets.json` 声明式镜像。

---

## 4. `-j1 ctest` 依据

| 理由 | 证据 |
|---|---|
| **固定根临时目录碰撞** | `test_io_raster_contract.cpp:26-33` / `test_io_vector_contract.cpp:25-32` 用非 pid 固定根，且同二进制内多个 TEST_CASE 复用同一 scratch 名（`"budget"` 3 个、`"move850"` 4 个）。`-j1` 是**当前的缓解手段**，G11-1 才是根治 |
| **GUI/平台资源** | 测试强制 `QT_QPA_PLATFORM=offscreen`；并行会放大 Qt 平台插件与 GDAL 全局配置的争用 |
| **确定性优先于吞吐** | 资源纪律原文：高价值设定是"串行 + 早失败"，不是吞吐 |
| **契约化** | 由包装器强制，selftest 第 3 组断言固定覆盖 |

**注意**：`-j1` 只**降低**并行碰撞概率，并未消除跨进程固定根冲突——后者需要 G11-1（改用 `sicnu_test::processScratchDir`）。

---

## 5. 磁盘 / 内存观察

### 5.1 ✅ 磁盘（实测）

| 观察 | 值 |
|---|---|
| 单次 configure 后的 `build-dev` | **3.4 G**（零 .exe 产物，纯 CMake/Ninja 元数据 + `_deps`） |
| C: 可用空间 / 使用率 | **430 G** / **77%** |

**判断**：以 3.4 G/树 的量级 + 430 G 余量，同机并行多个 worktree 的 build 树不构成磁盘压力。**瓶颈不是磁盘，是内存与 CPU 争用**（见 §3）。

### 5.2 ⚠️ 内存（未采集）

| 项 | 状态 |
|---|---|
| 物理内存总量 / 可用 | ❌ **NOT_MEASURED** |
| configure / 编译 / ctest 峰值 RSS | ❌ **NOT_MEASURED**（L3、L4 未跑） |

**未采集的原因**：① 本轮硬约束禁止重型编译，无可测过程；② 本机 Python 无 `psutil`，而 `scripts/run_perf_baseline.sh` 依赖 `nproc` / `/proc/meminfo` / `getrusage(RUSAGE_CHILDREN)`，**Linux-only，Windows 上不可用**。

---

## 6. ❌ G14 — perf guard 状态：NOT_RUN

| 子项 | 状态 | 说明 |
|---|---|---|
| `scripts/run_perf_baseline.sh` | ❌ **NOT_RUN** | 依赖 `nproc` / `/proc/meminfo`（Windows 退化为 0）；且需先有构建产物 |
| `scripts/perf_report.py merge/diff` | ❌ **NOT_RUN** | 需 `test_execution_benchmarks` 产出的 `SICNU_EXEC_BENCH_OUT` 目录 |
| `scripts/bench/perf_observatory_report.py compare` | ❌ **NOT_RUN** | 需 `test_perf_observatory` 产出目录 |
| `scripts/bench/perf_observatory_verify.py` | ⚠️ **已跑，FAIL** | 唯一可脱离构建的入口，见 §6.1 |

**已提交的基线**（仓库内已存在，非本轮产出）：
- `benchmarks/observatory/` — **22 个** `sicnu-perf-observatory/1` 记录
- `benchmarks/perf-observatory-baseline.md` — 热点证据（MEASURED / INFERRED / RECOMMENDED 三分标注）

⚠️ **重要**：该基线 md 自述机器为 **Linux x64 / GCC 16 / Debug / 16 核**，**与本机（Windows 11 / MSVC 19.38）不是同一台机器**。跨机器绝对耗时不可比——这正是 `ORACLES.md` O3 规定「时间只报增量、不断言相等」的原因。

### 6.1 ⚠️ 实跑结果：FAIL，并暴露 O1/O3 真空绿

```sh
PATH="/c/Qt/Tools/CMake_64/bin:$PATH" python scripts/bench/perf_observatory_verify.py \
    --build build-dev --jobs 1
```
```
PASS O1   run 1: 13/13 green (offline, fixed seed)
PASS O1   run 2: 13/13 green (offline, fixed seed)
FAIL O2   0 + 0 records validate against sicnu-perf-observatory/1 — a run produced 0 / 0 records, expected 13
PASS O3   structural indicators identical across the two runs
PASS O5   no assertion gates on an absolute time budget
PASS O6   a run writes nothing into the repository

summary: 5 passed, 1 failed
EXIT=1
```

`scripts/bench/perf_observatory_verify.py:145-170` 有一份**独立的** `run_ctest`，与 G12-2 修复前的 `verification_ladder.py` 是**同一缺陷**：
`return proc.returncode == 0, log` —— `ctest -R` 空匹配 exit 0 → 判 passed。

| 断言 | 真空绿机制 |
|---|---|
| **O1** | 空匹配 → `ok_a=True` → 打印**硬编码**的 `13/13 green`（与实际记录数无关） |
| **O3** | `sorted(set(rec_a) │ set(rec_b))` 在 0 条 vs 0 条间比较 → 空集恒等 → PASS |
| **O2** | **正确报 FAIL** —— 唯一带 `EXPECTED_RECORDS = 13` **显式计数下限**的断言 |

→ **O2 是这条链上唯一不空转的断言**，正因如此整体退出码为 1，假绿未逃逸；但 **O1/O3 措辞本身误导**，与 G12-1 属同一红线边界（"一个都没跑却声称全绿"）。

**建议方向**（❌ 不实现；`scripts/**` 属 R12-G 自有范围）：① `run_ctest` 追加 `--no-tests=error` 并做能力探测；② O3 先断言记录数 ≥ 13；③ 复用 `verification_ladder.py` 实现而非维护第三份拷贝。
**本轮处置**：未修，作为 G14 的 P1 发现记入 `REPRODUCERS.md` R9。

---

## 7. 资源纪律在本轮的执行情况

| 约束 | 遵守 | 证据 |
|---|---|---|
| 并行度 ≤ 2 | ✅ | 所有命令均为单进程 python/bash；未启动 `cmake --build` |
| ctest `-j1` | ✅ | 除空注册对照实验（显式 `-R ".*"` 单发）外未跑 ctest |
| `QT_QPA_PLATFORM=offscreen` | ✅ | 无 GUI 测试执行 |
| 不触发重型编译 | ✅ | `build-dev/tests/*.exe` 计数 = 0（本轮未新增） |
| 不联网 | ✅ | 所有探针为本地 stub / 本地文件 |

**遗留物**（需清理或纳入 `.gitignore`）：
- `build-dev.agent-build.lock`（worktree 根目录，未跟踪）—— 由 G06 接线后的构建租约产生，属预期产物，但当前 `.gitignore` **未覆盖** `*.agent-build.lock`。
