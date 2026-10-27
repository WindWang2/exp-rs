# R23-B TEST_RESULTS — 实测记录（2026-10-27，构建 build-r23-B，Release/系统 GDAL 3.13.3/Qt 6.12/OpenCV 5.0/GCC 16 系）

## 构建
- 配置：见 BASELINE.md（Ninja Release，ENABLE_TESTS=ON，SICNU_VENDOR_GDAL=OFF，SICNU_WITH_ONNX_RUNTIME=OFF，TRT/OV OFF（本机无 SDK，经 W1 stub gate 覆盖），OTB/EMBED_PYTHON OFF）
- 批量构建 3659/3659 步 exit 0（flock 串行 -j2；中断一次因 W2 测试文件缺 using namespace——修复后增量重建 exit 0，如实记录）
- 全程零真实编译 error（日志中 "error:" 命中均为源码字符串字面量/deprecation note）

## W1 — provider TU stub-compile gate（#1576/#1577）
| 项 | 结果 |
|---|---|
| RED（master TU 直接 stub-compile） | OV 3 error（m_artifactPath 未声明/selectDevice 未声明/const 锁非 mutable mutex）+ TRT 1 error（m_artifactPath 未声明）——输出见 RED_GREEN.md |
| GREEN（修复 TU）×2 | 两 TU 0 error ×2 |
| gate 变异反证 | 回喂 master OV TU → 3 error FAIL（exit 1）；恢复 → PASS |
| ctest `test_r23_b_provider_stub_compile` | **Passed 3.47s**（测试序号随配置漂移，以名称为准） |

## W2 — derived output.format 头契约门
| 轮 | 命令 | 结果 |
|---|---|---|
| RED | `tests/test_r23_b_derived_output_roles`（master 引擎） | **exit 42**：3 用例中 2 失败、27 断言中 6 失败——全部非法组合落 "failed to open input raster"（判别器实证：门不存在）；放行用例按设计通过 |
| GREEN1 | 同（修复引擎） | **exit 0，27/3 全过** |
| GREEN2 | 同 | **exit 0，27/3 全过** |

## W3 — run+metric 单事务（A5 闭合）
| 轮 | 结果 |
|---|---|
| GREEN1 | `tests/test_r23_b_run_metric_transaction` **exit 0，37/3**（原子性/冲突拒结名/崩溃窗口 A/B fork 用例全过） |
| GREEN2 | **exit 0，37/3** |
| 既有 A5 触发器用例（经新路径） | test_experiment_consistency_r4 内 "a failed run install leaves no orphan metric row" 随族全绿 |

## W4 — probeRemote 受控传输
| 轮 | 结果 |
|---|---|
| `test_io_remote_range`（probe 契约族，经新传输对本地 stub server 实跑） | **exit 0，25/6** |
| `test_offline_mode`（含 probeRemote 离线拒结名义） | **exit 0，34/10** |

## 域族回归（每族 GREEN1+GREEN2 双跑均 exit 0；下行数字为断言数/用例数）
| 族 | 结果 |
|---|---|
| test_model_runtime | **110,440/26** ×2（含 r21-B mask_threshold、#1706 羽化、r20-02 边缘混批用例；与 r21-B 基线计数一致） |
| test_experiment_consistency_r4 | **298/11** ×2 |
| test_dataset_core | **512/19**（×1 后再随机抽查族内关键用例——构建锁排队时限内；主双跑覆盖 experiment 域） |
| test_r22_data_security_consistency | **94/9** ×1+（其域未改；W4 传输策略面由其覆盖并绿） |
| test_r21_b_end_to_end | **4,142/2** ×2（导入→数据集→推理→run/lineage→bundle 全链） |
| test_io_vector_contract | **100,339/14**（#1491 本机判定：HEAD 无法复现 CI 失败 → 线上环境性归因，见 ISSUE_TRIAGE） |

## NOT_EXECUTED（如实声明）
- 线上 CI（按任务书不等待不运行）；GPU/TRT/OpenVINO 真机（无硬件/SDK——W1 stub gate 为替代编译证据）；TSan/ASan 轮（资源窗口）；Windows/macOS。
- test_dataset_core / test_r22_data_security_consistency 只双跑其中一轮完整族（另一轮为抽查）；两族所属域本轮生产代码未触碰（dataset 域零 diff；r22-data 域仅 probe_remote.cpp 属 geospatial）。
