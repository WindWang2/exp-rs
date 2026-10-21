# R19-01 LOOP_LEDGER — 迭代账本

循环协议：RECON → PLAN → RED/PROBE → MINIMAL FIX → GREEN×2 → INDEPENDENT REVIEW → FIX → REVIEW → REBASE → PR
规则：每 3 轮同因失败强制换假设；每 8 轮卡住重审前提。所有结论必须附命令与退出码。

## 环境事实（RECON 轮）
- 远端核对：`gh api repos/WindWang2/exp-rs/branches/master` → `e12a9afbc8883a474dc9ffcaf3345ae55863dc71` == 本地 `origin/master`（基线即最新，2026-10-20 核验）
- `git fetch origin` 直连 https://github.com:443 两次超时（134s）—— gh API 通道正常；push 时需重试/备用方案
- 开放 PR 仅 #1663（R18-A, head b5e9e6af1）/ #1664（R18-E, head bddbff58f）；r18-b/c/d 分支已被 master 包含
- 并行轨：r19-02（pi-mcp-workflow，构建中持锁）、r19-03（排队）；全局锁 `$GIT_COMMON_DIR/r19-global-j2-build` 实测串行
- 主机：40 核 / 62GB；本轨与全仓约束 **全局 -j2**（CLAUDE.md 禁 `-j$(nproc)`）
- 工具：gh ✔ python3 ✔ cmake 3.x ✔ ninja ✔（注意 shell `ninja` 别名默认 -j40，一律经 resource_guard/cmake --build 显式 -j2）

## 轮次记录

### 第 1 轮（RECON）
| 项 | 结果 |
|---|---|
| LIVE recon（PR/issue/分支） | 完成，见 OWNERSHIP.md 映射表 |
| 预存失败种子定位 | test_io_remote_validator.cpp:374 / test_io_fabric_multidim_11.cpp:331（等待构建后复现）|
| swarm P1/P2 核验 | 21 项已修、1 项文档化（#1576）、2 项未修（#1637-2、#1578）、2 项非本轨 |
| configure | 第一次失败（GSL 未找到，exit 1）；补 GSL/CURL 显式路径后重跑，已过 configure 进入排队等锁 |
| 改动 | 无源码改动（ownership 门前）|

### 第 2 轮（等待全局锁 → 构建；并行静态准备）
| 项 | 结果 |
|---|---|
| 全套规划文档 | OWNERSHIP / TASKS / RISK_REGISTER / DECISIONS / LOOP_LEDGER 落盘 |
| T1.1 RED | tests/test_io_paths.cpp 追加 `profileForPath resolves two-segment extensions without shadowing (#1637-2)`（zarr.json→Zarr 现为 GeoJSON，必红）|
| T1.2 RED | tests/test_rs_operator.cpp 追加 `execute enforces schema-declared numeric ranges with typed refusals (#1578)`（现无任何强制，必红）|
| 覆盖盘点 | range_cache 37 用例（含 206/416/代际/取消/磁盘层/私网）；chunk_graph 34（含 #1553 钉住 :754）；atomic_failures 14；txn_r4 3 —— A03/A04/A06 核心已有覆盖，补缺优先级降低 |
| 构建排队 | r19-02 持锁构建中（ninja 588+ 步）；本轨 resource_guard 正确等待 |

### 第 3 轮（构建失败 ×3 → 逐一定位，未换目标，只修构建面）
| 轮 | 现象 | 根因 | 处置 | 结果 |
|---|---|---|---|---|
| 3a | configure exit 1 @ SicnuDepDoctor GSL | GSL 不在任何默认搜索位 | 显式 vendor 路径 | 下一轮 |
| 3b | configure exit 1 @ tests/CMakeLists.txt:441 netcdf | netCDF 同上 | 显式 vendor 路径 | 下一轮 |
| 3c | configure exit 0；build 1155/1819 后 `collect2: ld 返回 1` | vendor 静态 libsqlite3.a/libexpat.a 非 PIC，qgis_core 链接失败 | 钉 /usr/lib 共享库（SQLite3/EXPAT），保留 1155 个 .o 增量 | 重跑（队尾 r19-02 之后）|

观察：三轮失败全为环境依赖解析（同因类），不是代码问题 —— 符合"3 轮换假设"规则的例外（每轮根因不同且实锤：报错行明确）。锁竞争：r19-02/r19-03 与本轨在同一全局锁上交替，等待期间完成 RED 测试编写、钉住核查、覆盖矩阵盘点、PR_BODY/文档。

### 第 4 轮（链接链路收敛 ×3 → build exit 0）
| 轮 | 失败 | 根因 | 处置 |
|---|---|---|---|
| 4a | qgis_core 链接 `libz.a 非 PIC` | vendor 静态 zlib 被 find_* 抓取 | 钉 /usr/lib 共享库（采纳 r19-02 已验证配方全集：ZLIB/ZSTD/PCRE2_8/QCA/EXPAT/SQLite3）|
| 4b | test 链接 `wrapper2_dgelsv_` 未解析 | libgdal.so DT_NEEDS libarmadillo.so.15（prefix 缺失，pw-seismic 闭包有）| `--rpath-link` 指向 pw prefix + LD_LIBRARY_PATH 运行期注入 |
| 4c | `libcblas.so.3: ssyrk_64_` 未解析（链接+运行两处）| 系统 OpenCV→libcblas(_64_) 但 libblas 被 rpath-link 解析成 Debian 版（无 _64_）；运行期同理（$L 里还有 Debian libpulsecommon 抢占系统对）| rpath-link 与 LD_LIBRARY_PATH 统一为 `/usr/lib:/usr/lib/pulseaudio:$L:$PWLIB`（系统优先，#730 反遮蔽同政策；ctypes 双序探针实证）|
| 4d | ctest exit 127 ×5 | **宿主 cmake 无 ctest 二进制** | run_tests.sh 改为直接执行 Catch2 二进制（绝对路径、带 spec 过滤、过全局锁）|

### 第 5 轮（RED → FIX → GREEN×2 完成）
- O-A1 双种子：**基线即绿**（#1548: 2 断言通过；#1456: 13 断言通过，POSIX lane）——#1662 的"仍红"客观纠偏为不可复现
- RED：T1.1 exit42×3 断言（zarr.json→GeoJSON 错配）；T1.2 exit42（radius=0 静默接受）；T1.3 exit42（5MiB 整读）
- 修复：40674def9（最长后缀匹配）/ d040e9325（execute 边界区间强制）/ 17b24d394（4MiB 有界读）
- GREEN-1：3/3 全过；GREEN-2：3/3 全过
- 同批二进制全量零回退：io_paths 63/15、io_products 44/5、**rs_operator 831/17（全体注册 operator 在新强制下无回退）**、remote_validator 232/27、multidim_11 82/6

### 第 6 轮（域基线 + R1 评审闭环）
- 域构建 83+2 目标：三轮排障（ninja -k1 中止链 → 删双 .so 重排 → **叶库架构修复**：validateParams 符号打破了 operators_core 叶库边界，processing 只链叶库 → 新 TU rs_param_validation 入叶库，双漏斗接自由函数）→ 双阶段 exit 0
- 域 sweep：首轮 22 失败 = 缺 PROJ_DATA（环境）→ 补齐后 **83 PASS + bench 设计性跳过 + 1 预存红**
- 预存红归因修复（501bcaedd）：Action::Advisory 枚举漂移 + F-A-13 记账 vs 10MiB 旧预算（公式推导留 TEST_RESULTS）→ 606,370 全绿
- O-A2 两遍：remote 家族 809 断言双绿
- R1 REQUEST-CHANGES → bfbd8d768 闭环（P1 生产可达性/有界读升级/契约注释/负向钉）

### 第 7 轮（进行中：R2 评审 → 打包 → PR）
- 远端复核：master 未动；#1665/#1666 新开（r19-03/r19-02），与本轨文件零重叠（实测 diff 比对）
- R2（增量+跨轨终审）子代理运行中
- 待：R2 处置 → .gitignore planning allowlist 追加（临界区，按前轨模式最小 append）→ planning 文档提交 → push → PR
