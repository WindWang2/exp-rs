# R22-02 (r22-data) TEST_RESULTS — 2026-10-26

环境：cachyos x86_64 / 40 核 / 62GB；vendor 前缀 `paleo-singlefactor-closure/vendor/prefix/usr` + link-shim；CMake Release / Unix Makefiles；跨轨双锁（`/tmp/exp-rs-r21-build.lock` + `/home/kevin/project/exp-rs-build.lock`）内 `-j4` 构建；运行时 `PROJ_LIB`/`LD_LIBRARY_PATH`/`QT_QPA_PLATFORM=offscreen` 就绪。

## 新增回归套件（GREEN，双跑）

`test_r22_data_security_consistency`：**All tests passed (87 assertions, 8 cases)** × 2 遍。
- isPrivateHost authority 终止 + userinfo（#1679）：16+ URL 形态
- resolveForPinning 字面量纵深（#1679，策略开/关两态）
- commit-marker 失败保留 journal+.bak 并可恢复（#1701）
- 陈旧 .bak 不成为回滚源（#1701）
- journal 不可写即报错（#1701）
- validateWindow int64 边界（#1703-1）
- 双池并发同 digest 8 轮全收敛（#1680）
- provenance 链接失败如实上报（#1702）

## #1679 独立 RED→GREEN（/tmp/r22red，master blob vs 修复版）

master @911e0b17：9 形态 7 漏判（127.0.0.1?@example.com 等判公网）；修复版 9/9 判私网。扩展 16 对抗形态见 RED_GREEN.md（14 直接一致，2 个经 curl 语义分析确认为期望过严非缺陷）。

## 受影响域族 sweep（pass 1 全绿，2026-10-26）

| 套件 | 结果 |
|---|---|
| test_io_remote_validator（Oracle-B2.1 SSRF 全量） | All passed (232/27) |
| test_io_range_cache（含新 #1700 几何切换 e2e + 更新 basis 格式） | All passed (323/38) |
| test_io_atomic_failures | All passed (79/14) |
| test_platform7_library（promotion 域） | All passed (283/19) |
| test_dataset_core | All passed (425/18) |
| test_fault_injection（artifact_pool 故障矩阵） | All passed (162/9) |
| test_artifact_store | All passed (126/12) |
| test_io_raster_contract（readMask/validateWindow 域） | All passed (2094/16) |
| test_io_fidelity | All passed (23/4) |
| test_data_manager_promote | All passed (31/4) |
| test_e2e_open_issues | All passed (443/47) |
| test_adversarial_m3 | All passed (393/6) |
| test_atomic_fs_caller_contract | All passed (287/18) |
| test_r15_B_geodata_persistence | All passed (8305/20) |

## 关键双跑（pass 2 = 安全/崩溃恢复/数据一致性，全绿）

test_r22_data_security_consistency ✅×2、test_io_remote_validator ✅×2、test_io_range_cache ✅×2、test_io_atomic_failures ✅×2、test_dataset_core ✅×2、test_platform7_library ✅×2（每族 EXIT=0，输出留档 /tmp/r22_pass2.log）。

## NOT_EXECUTED（诚实声明）

- GPU/TSan/Windows/macOS：无环境，NOT_EXECUTED。
- 全树 ctest（未构建全部 400+ 目标）：未执行；受影响 15 族已覆盖本轨全部改动面（geospatial/remote、geospatial/util、geospatial/raster、data/pool、dataset/promotion+store）。
- 线上 CI：按任务书不参与完成门。

## R1 修复轮（f22ec241bc）后重跑（双跑）

- test_r22_data_security_consistency：All passed (87/8) ×2 → 91/9（复活用例加入后）×2
- test_io_range_cache：All passed (327/39，新增 store 层 #1700 oracle) ×2
- test_io_atomic_failures / test_atomic_fs_caller_contract：All passed ×1+（R2 期间复跑）

## R2 修复轮（5fc544a6c6 + eb24ccb4dd）后最终验证（双跑 pass A/B）

| 套件 | pass A | pass B |
|---|---|---|
| test_r22_data_security_consistency | All passed (94/9) EXIT=0 | All passed (94/9) EXIT=0 |
| test_io_range_cache | All passed (327/39) EXIT=0 | All passed (327/39) EXIT=0 |
| test_io_remote_validator | All passed (232/27) EXIT=0 | （R2 无触碰，pass A 为准） |
| test_io_atomic_failures | All passed (79/14) EXIT=0 | All passed (79/14) EXIT=0 |
| test_dataset_core | All passed (425/18) EXIT=0 | All passed (427/18) EXIT=0 |
| test_platform7_library | All passed (283/19) EXIT=0 | （R2 无触碰） |

注：pass B 的 dataset_core 多 2 断言为测试内时间戳分支，两次均 EXIT=0 全过。

R2 复核自带验证：新套件 3 遍 9/9、[issue1680] 连续 10 遍 10/10、range_cache 全文件 39/39；Qt rename 前提（对已存在目标失败且不动目标）已由 R2 在本工具链实证。
