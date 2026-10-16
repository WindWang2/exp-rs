# R15-B 最终报告（FINAL_REPORT）

轨道 B：空间数据链路、数据集/实验存储与可恢复持久化。只审计/加固既有模块，没有新增 IO 框架、产品模块或框架迁移，没有重做已合并的修复，没有 cherry-pick（含 #1512）。

| 项 | 值 |
|---|---|
| 分支 / PR | `hardening/r15-B-geodata-persistence` / #1513（Draft） |
| BASE_SHA | `46791bc6`（origin/master，最后一次 fetch 仍为最新，无需 rebase） |
| 代码 HEAD | `f8e1f280`（之后只有文档提交） |
| 工作树 | `<repo 父目录>/exp-rs-r15-b-20261016`；构建树 `build-r15-b`、`build-r15-b-tsan`、`build-r15-b-asan`；RED 工作树 `<repo 父目录>/exp-rs-r15-b-red` |
| 工具 | `/teamwork-preview`、`/goal-loop`：未安装/等价执行（Oracle-Ledger 循环） |

## 1. 修复索引（全部 RED→GREEN×2，RED 证据见 TEST_RESULTS.md §2、§5.1）

| WP | 级别 | 缺陷 → 修复 | 提交 |
|---|---|---|---|
| B13 | P0 | 组发布中途被 kill，读者打开混代组 → 读侧 journal 闸门 | cec9b8d5、c85f475e |
| B12 | P0 | bundle 导入迁移符号链接/未校验文件 → 只迁移已校验的普通文件 | f840caf2 |
| B04 | P1 | resolver 对私网源抛异常 → 拒绝即空 token | f6e720a2 |
| B05 | P1 | rel=next 成环时重复拉取 → 检测重复续页 | 46e321d0 |
| B07 | P1 | ExperimentStore 扫描中途出错返回部分结果 → 类型化失败 | ba9487df |
| 第 5 项（B07 尾部 / R1-08） | P1 | DatasetStore / ExperimentStore 边、GovernanceStore 列表扫描吞掉中途错误 → 可选尾参 `QString *scanError`（向后兼容）+ Governance `stepFailed()`；lineage 图标记不完整 | c68d5e95 |
| 第 4 项 #1449 | P1 | close() 与并发调用 UAF（修复前 SIGSEGV）→ 四个 store 外置递归读写锁 | c68d5e95 |
| B06 | P1 | 外部内容变更后元数据仍描述旧字节、损坏内容仍为 Ready → 重新解析，失败即 Error + `asset.content_unreadable` | 790d302c |
| B15 | P2（含凭据落盘） | 远端源校验身份用原始 URL：签名落盘、轮换签名后永不判 stale → 去签名参数的资源身份 | adb6e525 |
| 第 7 项 CRLF | P3 | CRLF checksums.txt 使完好 bundle 被拒 → 容忍单个行尾 CR | d9ea2b01 |
| 第 7 项 闸门扩展（R1-04） | P2 | inspect*/validateCog/convert 输入不受 journal 闸门保护 → 加闸门 + 开后复查（缩窄 TOCTOU） | c4ed7d85 |
| #1505 T9 | P2 | 字面 `%` 文件名被当遍历拒绝 → 只拒绝解码后 NUL/`..` | bfe83d41 |
| #1505 T6 | P1 | 策略拒绝后回落到无守卫 /vsicurl/ → 拒绝即终局 | 7ed6dc02 |
| #1505 T3 / #1485 | P1 | DNS rebinding → 再解析校验并以 CURLOPT_RESOLVE 钉住连接 | fc712c72 |
| B09（TSan） | P2 | `GovernanceStore::stepFailed` 跨线程无锁读写 → atomic | f8e1f280 |

只加 oracle（首跑即绿）：B02、B11（审计无引擎缺陷）、B16、#1481 T6/T7/T8/T9/T19（FIXED_ON_MASTER）。
测量：B14（PERFORMANCE.md；PERF-2 的 keyset 改动实测更慢，已撤回）。审计记录：B03/B08/B09/B10（REVIEW_ROUND3.md）。

## 2. 测试（详见 TEST_RESULTS.md）
- RED：原始 5 个修复与本轮全部修复都在本机 RED 工作树重新复现。
- GREEN×2：本轨 3 个新测试 22/22；相关回归子集 211/211（两次）；`test_platform7_library`、`test_experiment_evaluation` 直接运行两次全部通过；`test_r12b_e2e_durability` 在子集内。
- Sanitizer：TSan 发现 1 条真实 race 并修复，之后 0；ASan+UBSan 0。
- `git diff --check`：通过。

## 3. 评审
三轮（REVIEW_ROUND1/2/3）均为同一 agent 的等价自审，**不是独立评审**。

## 4. 仍未关闭
- **P1：#1505 T11**（生产 CLI 安装点默认不开私网阻断）——安装点在 `src/cli/**`，不在本轨写权限；改结构体默认值会破坏大量 127.0.0.1 测试，属跨轨策略变更。补丁与 oracle 见 POST_1505_PATCHES.md §C。
- P2：R3-07（run 遍历读失败时返回部分结果，返回类型无错误通道）、R3-10（传输失败仍回落无守卫 /vsicurl/）、PERF-1（run_ids 镜像重写，需改数据模型）、PERF-2（listRunsByCursor 每页 COUNT）。
- P3：R1-05/06/07/09/10/11、R2-01、R3-01/02/03/04/08/09。开后复查无法消除"在一次 open 内开始并提交"的窗口。

## 5. NOT VERIFIED
- Windows、macOS。
- 全量 ctest；相关存储测试（test_store_fault_injection 等）的 sanitizer 构建。
- #1505 T6 的"重定向跳"形态（同一代码路径，无单独 oracle）。
- 远端 CI（按要求不等待）。

## 6. PR 状态
存在未关闭的 P1（#1505 T11），且 NOT VERIFIED 不只是平台项 → 按规则保持 **Draft**，未合并。

## 7. 所有权与受保护文件
写入限于 src/geospatial/**、src/data/**、src/dataset/**、src/experiment/** 与本轨测试；`tests/CMakeLists.txt` 只追加了 3 个测试块。
#1505 合并后才修改了 resource_uri.cpp、range_cache.cpp、offline_gate.cpp。未触碰 src/core/temporal_cube.cpp、src/python/**、src/runtime/gpu/gpu_plane.cpp、src/operators/runtime/**、src/processing/**、src/app/**、GitHub Actions，未写 master。
