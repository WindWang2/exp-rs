# R27-T3 TEST_RESULTS（全部本地实测，双跑）

## 环境
- worktree exp-rs-r27-t03-20261102T132444Z；build-dev（dev-default, Debug, ENABLE_TESTS=ON）
- Linux 6.18 cachyos；GCC 16.2.1（仓库自带 raise-compiler-stack.sh ICE 重试器）；链接器 lld（BFD 2.47 在 lismicnu_processing.so 的 DWARF reloc 上确定性失败 `reloc against .debug_str: error 4`，纯本地 cache 改 `-fuse-ld=lld` 绕过，仓库文件零改动）
- 编译 -j2 + flock /tmp/exp-rs-host-build.lock（与兄弟轨共享）；TSAN 通道本机不可用 → NOT_EXECUTED，以确定性 100× 串行重入断言（maxInFlight==1）代替，不伪称 TSAN green

## 新增测试（test_r27_t03_*）
| 二进制 | 结果（第1遍/第2遍） |
|---|---|
| test_r27_t03_approval_gate（214 断言/4 例：driver 拒 mint 矩阵、HMAC KAT vs python、replay 环满/过期逐出、ttl 界、人工通道） | 通过/通过 |
| test_r27_t03_dock_lifecycle（12 断言/6 例：100×串行 maxInFlight==1、GUI<200ms、取消重入违规==0、10×析构、arm 诚实矩阵、无 coordinator 意图） | 通过/通过 |
| test_r27_t03_containment_gdal_prefix（34 断言：14 逃逸形 Denied/DriverPath→containment 拒 + 良性回归） | 通过/通过 |
| test_r27_t03_secret_redaction（连字符键/值形状脱敏 + 科学证据对照组） | 通过/通过 |
| test_r27_t03_external_env_guard（注入键族 + LD_/DYLD_ 前缀 + 良性对照） | 通过/通过 |
| test_r27_t03_copilot_gate（含 R1-F1 真实 OpenAI 形状 + 字符串 blob） | 通过/通过 |
| r27_t03_cli_invalid_input（真实 CLI 二进制 ×4 形状 → 全部退出码 6） | 通过/通过 |
| r27_t03_python_worker（真实 daemon unix socket：回滚/SHM 回收断言 unlink/dtype 拒/A-B 隔离，4/4） | 通过/通过（直跑 python3 亦 OK） |

## 更新后的既有测试
| 二进制 | 结果 |
|---|---|
| test_agent_ops_core（47 例/491 断言，契约更新到 HMAC v2 + 人工通道） | 通过/通过 |
| test_agent_ops_ui_smoke / test_cli_agent_ops / test_agent_ops_driver | 通过/通过 |
| 邻接回归 test_r22_agent_containment（78 断言）/ test_r22_agent_env_keys（46 断言） | 通过 |

## 汇总命令与退出码
```
QT_QPA_PLATFORM=offscreen ctest -R "r27_t03_cli_invalid_input|test_r27_t03_(approval_gate|dock_lifecycle|containment_gdal_prefix|secret_redaction|external_env_guard|copilot_gate)::|test_agent_ops_core::|test_agent_ops_ui_smoke::|test_cli_agent_ops::|test_agent_ops_driver::|r27_t03_python_worker" -j1
# pass1: 100% tests passed out of 69   pass2: 100% tests passed out of 69   (exit 0 ×2)
# python worker 直跑: Ran 4 tests — OK ×2
```

## 首跑发现并修复的实现/测试缺陷（保留记录）
1. dock `armRepairApproval` 的 kTtlExcessive 用 const char* 指针比较 → TTL 超限永远报 MALFORMED（#1760 oracle 抓到；改值比较）
2. ring 测试驱动缺 re-projection（fake run 不带 recovery plan → 投影清空 → mint 正确拒绝 NO_PENDING_REPAIR_PLAN；修测试驱动）
3. HDF5 引号绝对路径按文件型 driver 走 DriverPath+re-contain（正确语义）；oracle 改为断言 containment 拒绝
4. 核心测试一处指针比较同病（修）

## 已知跳过
- test_worker_daemon_proxy.py 在 base 上即 3 errors（Python 3.14 FakeSocket 漂移，先于本轨，非回归；已在 /tmp 对照验证）
- test_r22_agent_ghost_algo_NOT_BUILT ×3 为哨兵测试（故意 NOT_BUILT，非失败）
