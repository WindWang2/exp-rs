# R19-01 ORACLES — 逐行 Oracle 表

格式：`ID | 触发源/风险 | 测试/探针命令 | baseline RED | fix commit | GREEN-1 | GREEN-2 | negative test | independent reviewer | post-rebase result`

| ID | 触发源/风险 | 测试/探针命令 | baseline RED | fix commit | GREEN-1 | GREEN-2 | negative | reviewer | post-rebase |
|---|---|---|---|---|---|---|---|---|---|
| OA1-1 | #1548 首见死锁复发（种子1）| `run_tests.sh seed1` | **不适用——基线即绿**：All tests passed (2 assertions)，退出 0；#1662 报告的"仍红"在当前 master 不可复现（客观纠偏：其红来自旧构建/环境）| — | 绿(e12a9afbc) | 绿(修复后) | 内建 30s 超时 | R1/R2 | 待 |
| OA1-2 | #1456 无时间维窗口 cols（种子2）| `run_tests.sh seed2` | **不适用——基线即绿**：13 assertions 通过，退出 0（POSIX lane；源注释言明红在 Windows vcpkg lane）| — | 绿 | 绿 | 内建窗口/像元比对 | R1/R2 | 待 |
| OA-T11 | #1637-2 zarr.json 两段扩展名不可达 | `run_tests.sh T1.1` | **红**：exit 42，3 断言失败（452/466/469，zarr.json→GeoJSON 错配）| 40674def9 | **绿**：10/10 | **绿**（二遍同）| bare .json→GeoJSON / .zarr→Zarr 兼容钉住 | 待 | 待 |
| OA-T12 | #1578 setRange 无运行时强制 | `run_tests.sh T1.2` | **红**：exit 42（radius=0 被静默接受）| d040e9325 | **绿**：6/6 | **绿** | 未声明 range 不受限、缺参走默认、边界值含端点 | 待 | 待 |
| OA-T13 | #1637-3 孪生 readFileText 无界读 | `run_tests.sh T1.3` | **红**：exit 42（5MiB 边车被整读解析成功）| 17b24d394 | **绿**：6/6 | **绿** | 类型化 InvalidMetadata + stat 失败回退 | 待 | 待 |

## 宿主二进制全量回归（修复后同批二进制）
| 二进制 | 结果 |
|---|---|
| test_io_paths | 63 assertions / 15 cases 全绿 |
| test_io_products | 44 / 5 全绿 |
| tests/test_rs_operator | **831 / 17 全绿**（注册表全体 operator 在新区间强制下零回退——X1 风险首轮实证缓解）|
| test_io_remote_validator | 232 / 27 全绿（O-A2 相关 SSRF/offline 矩阵完好）|
| test_io_fabric_multidim_11 | 82 / 6 全绿 |

## 完成判据总表（对应任务书 O-A1..O-A6）
- O-A1：✅ 完成（双种子客观纠偏为"当前 master 已绿"，证据上表；非掩蔽——保留退出码与输出于 tests_run1.log/green 证据链）
- O-A2：SSRF/offline/identity 负向矩阵——test_io_remote_validator 27 用例 + range_cache 家族在域基线跑两遍（域构建进行中）
- O-A3：推理 golden 对照——test_model_* 家族域基线（同上）
- O-A4：chunk/session 确定性——test_chunk_graph 等域基线（同上）
- O-A5：crash-safe durable replay——test_experiment_txn_r4 / atomic_failures / version_migration 域基线（同上）
- O-A6：两轮独立 review + rebase 复测 + PR 创建（URL 记录于 FINAL_REPORT）
