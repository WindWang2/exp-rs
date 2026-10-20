# R19-02 TEST_RESULTS（终版）

基线 `e12a9afbc`。CI 全仓失效（#1491）→ 全部本地。环境：Arch CachyOS x86_64、GCC 16、cmake 3.30.5+Ninja Debug、QT_QPA_PLATFORM=offscreen、全局锁 -j2、ctest/binary 直跑 -j1。
运行时 env：`LD_LIBRARY_PATH=<wt>/build-r19-02/link-shim:<vendor>/usr/lib/x86_64-linux-gnu PROJ_DATA=<vendor>/usr/share/proj`。

## 构建链（全部经 resource_guard 全局锁）
| # | 内容 | 结果 |
|---|---|---|
| 1 | configure×3 失败（GDAL→PROJ→GSL）→ vendor 提示集成功 | OK |
| 2 | 全量构建 | FAIL@1159/9339：vendor 非 PIC libsqlite3.a → 钉系统 .so |
| 3 | 定向构建 | FAIL：libcblas ztrsm_64_（隐式 -L vendor 使 blas 混解析）→ link-shim（system blas/lapack） |
| 4 | 定向构建 | FAIL：libQt6Core pcre2_16@10.47 → shim 增 pcre2 |
| 5 | 定向构建 | FAIL：vendor gdal wrapper2_*（前缀内无提供者）→ 采 r18-c 同款 `-Wl,--allow-shlib-undefined` |
| 6 | 定向构建 9 目标 | OK（catch2 发现期需运行时 env，后以直跑替代） |
| 7 | 族目标 14 + sicnu_geo_rs_cli + surface_mcp_host + regen | OK |
| 运行时 | vendor 前缀被外部裁剪（libarmadillo 消失）→ 生成带 SONAME 桩（armadillo/arpack/superlu + openblas wrapper2×26 惰性桩）；Qt6 全套 symlink 进 shim | 二进制可载入 |

（构建环境问题均为本机 vendor 前缀共享漂移，与仓库代码无关；configure.sh 记录全部决策）

## 测试矩阵（×2 = 连续两轮）
| 套件 | run1 | run2 |
|---|---|---|
| scripts/dev unittest（含 test_merge_loss_audit 7 例） | OK | OK |
| pi Node 全套 | 39/39 | 39/39 |
| test_science_context_broker（T0） | 40例/183 ✓ | ✓ |
| test_science_context_concurrency（T0） | 14例/10130 ✓ | ✓ |
| test_science_context_live_authorities（T0；PROJ_DATA 后） | 15例/197 ✓ | ✓ |
| test_r19_02_workflow_cancel_order（T1 新） | 4例/1009 ✓ | ✓ |
| test_workflow_cancel（回归） | 8例/74 ✓ | ✓ |
| test_explain_validator / guidance_coverage / builder（T2） | 11/21、4/218、13/70 ✓ | ✓ |
| capability_knowledge（漂移门） | 16例/1592 ✓ | ✓ |
| test_mcp_server | 31例/9686 ✓ | ✓ |
| test_surface_parity | 12例/5484 ✓ | ✓ |
| test_surface_e2e | 1例/4202→4296 ✓ | ✓ |
| test_surface_snapshot（T7） | projection+wire 腿 ✓；registry 腿 1 败（CT-6 红区） | 同（确定性） |
| test_surface_protocol / protocol_r4 | 10例/70-74、11例/2609 ✓ | ✓ |
| test_workflow_execution_plane / recovery | 8例/96、15例/163 ✓ | ✓ |
| test_r14_C_mcp_workflow_cancel | 2例/21 ✓ | ✓ |
| test_agent_ops_core / task_center_executor | 47例/471、28例/101 ✓ | ✓ |
| test_agent_loop_seams | 10例/101 ✓ | ✓ |
| test_plugin_capabilities | 14例/129 ✓ | ✓ |

## T0 RED 证据（恢复 src 前，同一二进制）
- broker：`CHECK( snapshot->revision() == 7 )` → `9 == 7`（陈旧视图读到新 authority）
- concurrency：`coherentAnswers == observations` → `1 == 600`

## T6 证据
- 页面 33384B→32521B（预算 32768）；gen-pages --check zero diff；pi 39/39。

## NOT_EXECUTED
- CI（#1491）；Windows/macOS；plugin_host 进程族全量（本轮范围外，后续轮）。
