# R20-03 TEST_RESULTS（终版）

基线 `28e1891390`（origin/master live 复核，无新提交）。CI 全仓失效（#1491，2026-10-11 起）→ 全部本地。
环境：Arch CachyOS x86_64、GCC 16、cmake+Unix Makefiles Debug、`QT_QPA_PLATFORM=offscreen`；构建经全局锁 `-j2`（与 R20-01/02/04/05 串行）；测试直跑 `-j1`（二进制 RPATH 解析 /usr，无需 shim）。依赖：纯系统库配方（R19-02 vendor 前缀已消失，复刻 R20-02 配方），configure exit 0。

## 构建（全部经全局锁，原始日志 /tmp/r20_03_{configure,build,cycle,final_cycle}.log）
| 步骤 | 结果 |
|---|---|
| configure | exit 0 |
| 定向构建 21 测试目标 | OK（1888 targets；stop 于 test_plugin_host=SICNU_EMBED_PYTHON 门控目标，剔除） |
| plugin 11 目标 + exprs_ipc 重链 + cli + host | OK |
| RED 构建（master src + HEAD tests，4 目标） | OK |
| GREEN 重建（HEAD src） | OK |
| capability_knowledge_tool + 重建 capability 测试 | OK |
| master 对照：host_process + 4 in-process plugin 测试 | OK |

## RED 证据（master src + HEAD tests 同一环境，/tmp/r20_03_red.log）
| 二进制 | 结果 |
|---|---|
| test_exprs_ipc | 29 例 2 败：:111 `salvaged.size()==4` → `0==4`（W4 master 不抢救）；:292 `REQUIRE_FALSE(decode)` → 非对象 result 被 master 接受（W1） |
| test_capability_knowledge | 17 例 3 败/12 断言：validator string/array schema_version、loader catalog/knowledge/relations reload 抛 Json::LogicError（W2）+ R2 门 |
| test_harness_evals | 19 例 1 败：:1083 decision_record 错型参数异常穿透（W3） |
| test_agent_ops_core | 47 例全过（W5 死字段无行为 RED——结构性变更，编译+回归守护） |

## GREEN ×2（HEAD）
| 二进制 | run1 / run2 |
|---|---|
| test_exprs_ipc | 29 例/176 断言 ✓ / ✓（RC=0×2） |
| test_capability_knowledge | 17 例/1637 断言 ✓ / ✓ |
| test_harness_evals | 19 例/295 断言 ✓ / ✓ |
| test_agent_ops_core | 47 例/471 断言 ✓ / ✓ |
| test_surface_parity | 12 例/5506 断言 ✓ / ✓（cli 宿主补建后） |
| test_surface_e2e | 4810 / 4674 断言 ✓ / ✓ |
| test_r14_C_mcp_workflow_cancel | ✓ / ✓（另 3 轮中 1 轮通过后 SIGSEGV，见下） |
| pi Node 套件 | 39/39 ×3（一次 38/39 为双构建重载 flake，复跑 2 次全绿） |

## Family ×2（家族运行 1+2，13 二进制全绿）
test_mcp_server（31 例/9686 断言级）、test_agent_ops_task_center_executor、test_workflow_cancel、test_r19_02_workflow_cancel_order、test_workflow_execution_plane、test_workflow_recovery、test_agent_loop_seams、test_plugin_capabilities、test_science_context_{broker,concurrency,live_authorities}、test_surface_protocol、test_surface_protocol_r4 —— 34 次运行 27×RC=0（其余 7 次为下述已知项/路径项）。

## W8 plugin_host 进程族（R19-02 NOT_EXECUTED 债，本轮补跑）
| 二进制 | 结果 |
|---|---|
| test_plugin_host_allowlist / test_plugins_runtime_host / test_plugin_execution_barrier / test_plugin_manifest / test_exprs_plugin_system / test_plugin_channel_parity_r4 | GREEN（RC=0） |
| test_plugin_host_process | 33 例 1 败（:1259 sawTypedRefusal）——**master 同构**（master 对照逐字节相同失败） |
| test_plugin_host_boundary_r4（1）/ test_plugin_lifecycle_halffail_r4（6）/ test_plugin_lifecycle_unload_order_r4（1）/ test_exprs_plugin_loader（16） | **master 同构**（四者 master==HEAD 失败集逐字节相同） |
| test_plugin_host / test_python_plugin_host / test_python_plugin_manager | NOT_EXECUTED：目标在 `if(SICNU_EMBED_PYTHON)`（本配置 OFF）内不存在 |

## KNOWN_BASELINE_FAIL（预存红，master==HEAD 同构证据，非本 PR 引入）
1. **CT-6**（04 轨红区）：test_surface_snapshot registry 腿确定性失败 ×2，首个失败工具 `cartography:chart_list`（R19-02 CT-6 的 7 cartography 工具 schema 缺项集）；projection+wire 腿绿（7 例中 6 过，17695/17696 断言）。
2. **plugin 诊断码族 5 二进制**（上表）：含 E5005 访问模型拒绝未触发、symlink TOCTOU 逃逸未观测、LibraryLoadFailed/TrustRejected/ManifestInvalidJson 诊断缺失等。本环境（纯系统依赖、GCC16）下 master 同构失败；属 plugins/sdk 域预存问题，候选工单移交（部分可能是环境敏感：fixture .so 行为）。
3. r14_C 一次通过后 SIGSEGV（run1 of 4）：不可复现（后续 3 轮全绿），记录为疑似负载相关 teardown flake（与 R19-02 曾记录的段错误同类）。

## 修复链副产物（跨轨修复的实证）
- rs:sam_classify sidecar drift：r18-a (#1663) 加 operator limitation 未再生 sidecar → drift 门 master 红 → gen-meta 再生（157 sidecar 确定性，单文件 diff）+ gen-pages 页面再生（20728B<32KiB）→ 门全绿。
