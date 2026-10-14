# R12-G DECISIONS — 决策日志

- **方向**: R12-G · 分支 `hardening/r12-g-build-test-offline` · Worktree `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`
- **基线**: `origin/master = 28abef358`
- **格式**：编号 / 决策 / 备选 / 理由 / 反证（什么情况下这个决策应当被推翻）

## 1. 总表

| # | 决策 | 类型 |
|---|---|---|
| D1 | 不删除任何测试，不顺手改他人方向文件 | 红线 |
| D2 | 不顺手改他人方向文件（`src/**`、`tests/test_*.cpp`） | 红线 |
| D3 | netcdf 用 `find_path` 成对探测，而非把 `FATAL_ERROR` 放宽为 `WARNING` | 技术 |
| D4 | `verification_ladder` 用 `--no-tests=error` 且先做能力探测 | 技术 |
| D5 | `executed_items == 0` 判失败 | 技术 |
| D6 | `resource_guard --parallel` 从 `choices=(1,2)` 改为 `int` 且显式拒绝 `<1` | 技术 |
| D7 | `build.sh` 用长租约包裹 cmake/ctest，且失败开放（fail-open）+ 显式告警 | 技术 |
| D8 | 未修项的原因与移交方式 | 流程 |

## D1 · 不删除任何测试

**决策**：本轮**零测试删除、零测试禁用、零 `RUN_SERIAL` 补偿式绕过**。所有"让红灯变绿"的手段一律不用。

**备选**：
- (a) 对失败的 suite 加 `RUN_SERIAL TRUE` 掩盖并行碰撞（G11-1 的最小修法之一）
- (b) 对 `_exit` 相关的 suite 直接不注册
- (c) 调整 `ctest -R` 模式跳过问题套件

**理由**：
1. 本方向的使命是**提升验证链的可信度**，任何"让红灯变绿"的操作都与使命自相矛盾。
2. 审计报告明确把「不得删除测试」列为 P0 反作弊红线（`AUDIT_G04_G05_G12.md` §3.9）。
3. 被条件门吞掉的 41 批测试（G04-1）本身就是"看不见的删除"——本轮修复 G12-1/G12-2 正是为了让这种消失**变得可见**。

**反证**：若某个 suite 被证明是纯粹的编译阻塞项且拥有方确认废弃，需由 owning direction 显式移除并记录，不在本轮范围内。

## D2 · 不顺手改他人方向文件

**决策**：`src/**` 完全不碰；`tests/test_*.cpp`（A/C 改名清单）不碰；`tests/CMakeLists.txt` 仅做**加法式**最小改动并走短租约。

**备选**：
- (a) 把 G05-3（`src/core/CMakeLists.txt:2754-2756`）一起修了，反正"只是加个 if 守卫"
- (b) 把 G02 的 20 个 `_exit` 批量替换成 `return`

**理由**：
1. **冲突成本**：`src/**` 正被 PR #1483（42 文件）与 #1484（10 文件）并行写入。三方同时改会让 rebase 变成不可解的人工合并。
2. **验证预算**：(a) 需要完整 Windows 构建 + 链接才能证伪；(b) 需要跑通全部相关 suite。本轮禁止重型编译。
3. **危害不对称**：这些"顺手修"如果错了，会把**已知缺陷（泄漏）换成未知缺陷（退出期崩溃）**——后者更贵。

**反证**：若 owning direction 明确请 R12-G 协助，或这些文件进入 R12-G 的正式所有权范围，此决策失效。

## D3 · netcdf 用 `find_path`，不放宽为 `WARNING`

**决策**：把 `tests/CMakeLists.txt:427` 的单点 `find_library` 改为 **`find_library` + `find_path` 成对探测**，守卫条件升级为 `if(NOT NETCDF_LIBRARY OR NOT NETCDF_INCLUDE_DIR)`，**保留 `FATAL_ERROR`**。

**备选**：
- (a) **放宽**：把 `FATAL_ERROR` 降级为 `WARNING`，让 configure 在 netcdf 缺失时继续（G05-2 的方向）
- (b) **更严**：直接用 `external/mdal/cmake/FindNetCDF.cmake`（`FIND_PATH` + `FIND_LIBRARY` + `FIND_PACKAGE_HANDLE_STANDARD_ARGS`，目前全仓库零命中）

**理由**：
1. **诊断质量**：原缺陷的失败形状是"configure 绿、编译红，一次炸 3 个 target"。`find_path` 把失败点前移到 configure，且报错文案带上库/头的**实际值**，是可操作的。
2. **不改变依赖契约**：(a) 会把"注释自称 optional、实现是 required"这个矛盾**固化**——注释与实现依然不一致，只是方向反转。而 `find_path` 是在**现有语义内**修复假成功，不引入新的语义分歧。
3. **最小改动面**：(b) 引入了新的模块依赖和路径约定，收益与 (b) 相当但改动面更大。
4. **副作用可控**：3 行 `target_include_directories` 是纯加法，不影响其他 target。

**⚠️ 已知未解决**：G05-2（optional 注释 vs required 实现 + `vcpkg.json` 无 netcdf）**仍在**。本决策不解决它，只是不让它变得更糟。后续需由依赖所有权方二选一并落地。

**反证**：若项目决定 netcdf 正式降级为 optional（改 `vcpkg.json` + 依赖摘要 + 测试注册一并跳过），则 D3 应被 D3' 取代，但仍需保留 `find_path` 以避免"库在头不在"的假成功。

## D4 · `--no-tests=error` + 能力探测

**决策**：`run_ctest` 追加 `--no-tests=error`，但**先用 `ctest --help` 探测该选项是否存在**，不支持则不追加；并把 `No tests were found` 显式映射为 `failed` + 可读原因。

**备选**：
- (a) 直接硬加 `--no-tests=error`
- (b) 改用 `ctest -N` 计数断言 > 0（与 `chain_gate_census.sh:144` 既有做法一致）

**理由**：
1. **(a) 的风险**：`--no-tests` 是 CMake ≥ 3.18 才有的。硬加会让**所有** ctest lane 在老 CMake 上退化成 usage error——把"防假绿"变成"全红"，是更坏的静默失败（症状掩盖了原因）。
2. **(b) 的代价**：多一次子进程调用，且 `-N` 与实际执行存在 TOCTOU 窗口。
3. **探测的代价极低**：每个进程只跑一次，结果缓存在 `_CTEST_NO_TESTS_SUPPORTED`。
4. **双重保险**：即使探测失败（降级路径），`No tests were found` 的文本兜底仍然生效。

**实测证据**
```
supports --no-tests: True
STATUS = failed
DETAIL = ctest matched no test for pattern '.*': the lane ran nothing, so it proves nothing (registration drift?)
```
底层对照：`ctest -R ".*"`（空注册）exit **0**；加 `--no-tests=error` exit **8**。

**反证**：若仓库把 CMake 最低版本提到 ≥ 3.18 且该约束被 CI 强制，能力探测可简化为直接追加。

## D5 · `executed_items == 0` 判失败

**决策**：聚合阶段统计 `executed_items` / `planned_items` 写入 JSON；`planned > 0 且 executed == 0` 时 `overall_ok=False` 并打印 `OVERALL NOTE: …`。**不改变 item 层语义，也不改变 `--strict` 语义。**

**备选**：
- (a) 给 `overall` 增加第四态（如 `"insufficient-evidence"`）
- (b) 把 `--strict` 设为默认

**理由**：
1. **(a) 会破坏下游契约**：`collect_readiness.py`（`:207-214`）与 JSON 消费方已按 `passed`/`attention` 两态实现。加第三态需要同步改下游，超出最小修复面。
2. **(b) 会误伤**：`--strict` 是"连 skipped 也不许有"的强约束。默认开启会让大量合法的环境性 skip 变成红灯。
3. **精确命中红线**：本决策只针对"**一个都没跑**"这一种形状——这正是"不得默默跳过必需 suite"的核心边界。跑了但部分 skipped，仍然诚实。
4. **证据**：`executed_items=0, planned_items=4` → `OVERALL: ATTENTION`，exit 1。

**反证**：若未来需要区分"跳过"与"未构建"两类间隙，可再加字段，但不应把当前两态聚合改坏。

## D6 · `--parallel` 从 `choices=(1,2)` 改为 `int` 且显式拒绝 `<1`

**决策**：`--parallel` 接受任意 `>=1` 整数；`<1` 时打印 `refused: --parallel must be >= 1, got N` 并 `return EXIT_REFUSED`。

**备选**：
- (a) 保留 `choices=(1,2)`，让 `build.sh` 自己裁剪后再传
- (b) 放宽为 `int` 但不校验（`<1` 静默按 1 处理）

**理由**：
1. **权威归属**：`-j` 上限的唯一权威是 `scripts/build.sh`（`JOBS_CAP` + `resolve_jobs()` + `SICNU_BUILD_JOBS_CAP` 升级路径）。`resource_guard.py` 是**被调方**，不应 duplicate 策略。
2. **(a) 的实际故障**：抬高 `SICNU_BUILD_JOBS_CAP` 后，`build.sh` 会把 `--parallel 4` 传给守卫，argparse 直接报错退出 2 → **合法的提额路径被守卫挡死**。这是一个真实的设计缺陷（策略两处定义必然漂移）。
3. **(b) 的危害**：`--parallel 0` 会让 `clamp_parallelism` 生成 `-j0`；`make -j0`/`ninja -j0` 语义未定义或无限并发，是典型的"静默忽略非法输入"。
4. **显式拒绝优于静默钳制**：`<1` 是调用方 bug，应该响亮失败而不是猜意图。

**实测证据**：selftest 第 4 组断言（本轮新增）通过，`ok: busy build dir refused (exit 75); payload never ran`。

**反证**：若守卫改为"唯一权威"（即 `build.sh` 从守卫读 cap），则 `choices` 可恢复——但那是一次更大的重构，不在本轮。

## D7 · `build.sh` 长租约包裹 + 失败开放（fail-open）

**决策**：
- `cmd_build` / `cmd_test` 的 payload **一律**包进 `resource_guard.py` 租约，默认 `--wait 1800`（`SICNU_BUILD_GUARD_WAIT` 可调，`=0` 快速失败）
- 租约**不可用**（脚本缺失或找不到 python）时：`note` 显式告警后**继续跑**，不静默跳过；`SICNU_BUILD_REQUIRE_GUARD=1` 可升级为拒绝；`SICNU_BUILD_NO_GUARD=1` 为显式退出

**备选**：
- (a) **失败关闭（fail-closed）**：租约拿不到就直接拒绝构建
- (b) **静默跳过**：守卫不可用时无声继续（修复前的实际状态）

**理由**：

| 维度 | 选 fail-open 的原因 |
|---|---|
| 故障模式 | fail-closed 会让"python 不在 PATH"这类**环境问题**表现成"构建被拒绝"，排障信息被稀释 |
| 可得性 | 守卫是**性能/公平性优化**，不是正确性前提。没有它构建依然正确，只是可能与其他 agent 叠加 |
| 可观测性 | 关键在于**不静默**——`note` 到 stderr，且提供 `REQUIRE_GUARD=1` 给需要强约束的场景 |
| 渐进收紧 | 先 fail-open + 告警跑起来，验证稳定后再考虑收紧默认值 |

**⚠️ 取舍的代价（如实记录）**：在 fail-open 下，如果 python 缺失，多 agent 争用问题**依然存在**，只是会打印告警。这是为"不让环境噪声伪装成构建拒绝"付出的代价。

**(b) 为什么不行**：修复前守卫零调用点，等价于永久 fail-open 且**没有任何提示**——使用者完全不知道资源纪律没生效。这正是 G06 的缺陷本体。

**实测证据**：RED 探针输出 `PROBE: RED — the entry point built while another process held the lease`；修复后 selftest ALL PASS。

**反证**：若观测到因守卫缺失导致的 OOM/连锁失败，应翻转默认值为 `REQUIRE_GUARD=1`。

## D8 · 未修项的原因与移交

**决策**：G02（19 个 `_exit`）、G03（6 项）、G04-1（41 门）、G05-2/3、G08/G09/G10/G11 全部**仅审计不修复**，作为跨方向发现归档。

**备选**：
- (a) 全部修完再交付
- (b) 只修自己包内的，其他不记录

**理由**：

| 未修项 | 阻塞类型 | 具体原因 |
|---|---|---|
| G02 残余 19 个 `_exit` | **所有权 + 验证预算** | TU 属其他方向；改成 `return` 会触发 QGIS 静态析构顺序崩溃风险（`qt_lifecycle.h:2-21`），必须能跑 Windows suite 才敢改 |
| G03-1 ~ G03-6 | **验证预算** | G03-1 在 Windows 不复现（`closesocket` 会唤醒 `accept`，实测 10038），需在 Linux CI 上验证 |
| G04-1（41 个条件门） | **共享文件冲突** | 修复点在 `tests/CMakeLists.txt`，与 #1483/#1484 并行写入 |
| G05-2（optional 语义） | **契约决策未定** | 需依赖所有权方在"降级为 optional"与"承认 required"间二选一 |
| G05-3（`src/core/`） | **越权** | `src/core/CMakeLists.txt` 不属 R12-G |
| G08/G10（打包/脚本） | **需真实产物** | 需 Windows 构建产物 + `dumpbin` 才能证伪 |
| G13（AppImage） | **平台不可能** | 本机 Windows；且 `appimage-tool-checksums.txt:11-12` 仍为 PENDING，`fetch_tool` 恒定 exit 1 |
| G14（perf guard） | **未执行** | 未采集任何 baseline |

**移交方式**：每份审计报告都附带 file:line、最小 RED 命令、建议修复方向（明确标注"不实现"）。接收方只需照抄模板即可，无需重新定位。

**反证**：若 team-lead 明确授权扩大所有权范围，或验证预算解除（允许重型编译），上述阻塞可逐条解除。

## 附：决策覆盖自检

任务要求覆盖的 8 个决策点：

| 要求 | 覆盖 |
|---|---|
| 不删除任何测试 | ✅ D1 |
| 不顺手改他人方向文件 | ✅ D2 |
| netcdf 用 find_path 而非放宽为 WARNING | ✅ D3 |
| verification_ladder 用 `--no-tests=error` 且做能力探测 | ✅ D4 |
| `executed_items == 0` 判失败 | ✅ D5 |
| `resource_guard --parallel` 从 `choices=(1,2)` 改为 `int` 且显式拒绝 `<1` | ✅ D6 |
| `build.sh` 用长租约包裹 cmake/ctest 且失败开放的取舍 | ✅ D7 |
| 未修项的原因 | ✅ D8 |
